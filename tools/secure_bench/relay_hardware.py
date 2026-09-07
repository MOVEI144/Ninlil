"""Three verified kits: provision without backups; hash-check preserved data."""
import hashlib,json,struct,sys
from pathlib import Path
from esptool.cmds import attach_flash,verify_flash,write_flash
import esptool
import hardware as hw

hw.IDENTITIES['c']=('COM7','E0:72:A1:D7:77:28')
root=hw.EVIDENCE

def ranges(board):
    return [(0x9000,0x7000),(0x190000,0x400000)] if board=='c' else [(0x9000,0x7000),(0x200000,0x24000)]

def digests(esp,board):
    return [{'offset':a,'size':n,'md5':esp.flash_md5sum(a,n).lower()} for a,n in ranges(board)]

def flash(board):
    folder=root/f'relay-20260908-{board}'
    hashes=json.loads((folder/'hashes.json').read_text())
    for name,digest in hashes.items():
        assert Path(name).name==name and hashlib.sha256((folder/name).read_bytes()).hexdigest()==digest
    node={'a':1,'b':2,'c':3}[board]
    header=(folder/'sdkconfig.h').read_text().splitlines()
    for line in [f'#define CONFIG_NINLIL_NODE_ID {node}', '#define CONFIG_NINLIL_RF_FREQUENCY_HZ 921400000', '#define CONFIG_NINLIL_RF_TX_POWER_DBM -9', '#define CONFIG_NINLIL_RF_SF 7', '#define CONFIG_NINLIL_RF_BW_125 1', '#define CONFIG_NINLIL_RF_GATE_POLARITY_CONFIRMED 1', '#define CONFIG_NINLIL_RF_GATE_RX_ACTIVE_HIGH 1', '#define CONFIG_NINLIL_RF_TX_ENABLE 1', '#define CONFIG_NINLIL_RF_REGION "JP"']:
        assert line in header,line
    local=(root/'relay-20260908-local.log').read_text()
    assert local.count('100% tests passed, 0 tests failed out of 27')==4
    assert 'libedhoc/submodule pins and committed compatibility patch ledger PASS' in local
    images=[(0,(folder/'bootloader.bin').read_bytes()),(0x8000,(folder/'partitions.bin').read_bytes()),(0x10000,(folder/'app.bin').read_bytes())]
    for start,data in images:
        end=(start+len(data)+4095)//4096*4096
        assert all(end<=a or start>=a+n for a,n in ranges(board))
    esp=hw.connect(board)
    try:
        preserved=digests(esp,board)
        with (root/f'relay-20260908-preserved-{board}.json').open('x') as out:json.dump(preserved,out,indent=2)
        control=0x5b4000 if board=='c' else 0x224000
        sessions=0x5d4000 if board=='c' else 0x244000
        layout={}
        for off in range(0,len(images[1][1]),32):
            magic,typ,sub,start,size,name,flags=struct.unpack('<HBBII16sI',images[1][1][off:off+32])
            if magic!=0x50aa:break
            layout[name.split(bytes([0]))[0].decode()]=(start,size)
        assert layout['ninlil_control']==(control,0x20000) and layout['ninlil_sessions']==(sessions,0x40000)
        assert layout['nvs']==(0x9000,0x6000)
        if board=='c':assert layout['ninlil_st']==(0x190000,0x400000)
        else:assert hashlib.sha256(esp.read_flash(0x8000,0xc00)).hexdigest()=='59514879595ffbb48032cd5ea36f6d7b27479f00b35304ec1a934fef64565a47'

        for start,n in [(control,0x20000),(sessions,0x40000)]:
            assert esp.flash_md5sum(start,n).lower()==hashlib.md5(bytes([255])*n).hexdigest(),'Nonempty test area; stop without erasing'
        if board=='c':
            entries=[]
            table=esp.read_flash(0x8000,0xc00)
            for off in range(0,len(table),32):
                magic,typ,sub,start,size,name,flags=struct.unpack('<HBBII16sI',table[off:off+32])
                if magic!=0x50aa:break
                entries.append((typ,start,size,name.split(bytes([0]))[0]))
            assert (1,0x190000,0x400000,b'ninlil_st') in entries
        esp=esptool.run_stub(esp);attach_flash(esp)
        write_flash(esp,images,flash_freq='80m',flash_mode='dio',flash_size='8MB',no_progress=True)
        verify_flash(esp,images,flash_freq='80m',flash_mode='dio',flash_size='8MB')
        assert digests(esp,board)==preserved
        print('RELAY_FLASH_VERIFIED',board,hashes['app.bin'],flush=True)
    finally:esp._port.close()


def stop(board):
    esp=hw.connect(board)
    try:
        preserved=json.loads((root/f'relay-20260908-preserved-{board}.json').read_text())
        assert digests(esp,board)==preserved
        print('PRESERVED_DATA_UNCHANGED',board,flush=True)
        # Restore known TX-disabled build images for A/B; C receives a new
        # frequency-zero image using the data-preserving partition table.
        if board=='c':
            folder=root/'relay-20260908-c-idle'
            images=[(0x10000,(folder/'app.bin').read_bytes())]
        else:
            folder=root/('init-a-fix-build' if board=='a' else 'init-b-build')
            hashes={line.split()[1]:line.split()[0] for line in (folder/'SHA256SUMS').read_text().splitlines()}
            images=[(0,(folder/'bootloader.bin').read_bytes()),(0x8000,(folder/'partition-table.bin').read_bytes()),(0x10000,(folder/'ninlil_m1.bin').read_bytes())]
            for name in ('bootloader.bin','partition-table.bin','ninlil_m1.bin'):
                assert hashlib.sha256((folder/name).read_bytes()).hexdigest()==hashes[name]
        config=(folder/'sdkconfig').read_text().splitlines()
        assert 'CONFIG_NINLIL_RF_FREQUENCY_HZ=0' in config and '# CONFIG_NINLIL_RF_TX_ENABLE is not set' in config
        if board=='c':
            for name,digest in json.loads((folder/'hashes.json').read_text()).items():
                assert hashlib.sha256((folder/name).read_bytes()).hexdigest()==digest
        esp=esptool.run_stub(esp);attach_flash(esp)
        write_flash(esp,images,flash_freq='80m',flash_mode='dio',flash_size='8MB',no_progress=True)
        verify_flash(esp,images,flash_freq='80m',flash_mode='dio',flash_size='8MB')
        assert digests(esp,board)==preserved
        esp._port.timeout=0.2;esp._port.reset_input_buffer();esp.hard_reset()
        import time
        data=bytearray();deadline=time.monotonic()+45
        while time.monotonic()<deadline and b'operational RF disabled: frequency is unset' not in data:
            data.extend(esp._port.read(min(max(esp._port.in_waiting,1),4096)))
            assert len(data)<1024*1024
        assert b'freq=0 tx=disabled' in data and b'NINLIL_HIL_INIT result=PASS' in data
        with (root/f'relay-20260908-idle-{board}.log').open('xb') as out:out.write(data)
        print('RELAY_FINAL_TX_DISABLED',board,flush=True)
    finally:esp._port.close()

if __name__=='__main__':
    action,board=sys.argv[1:]
    assert board in ('a','b','c') and action in ('flash','stop')
    (flash if action=='flash' else stop)(board)
