"""Identity-checked September 2026 lab provisioning and readback; never erase-all."""
import hashlib
import json
import sys
from pathlib import Path

import esptool
from esptool.cmds import attach_flash, detect_chip, verify_flash, write_flash
from serial.tools import list_ports

ROOT = Path(__file__).resolve().parents[2]
EVIDENCE = ROOT / '.verify-m1-evidence'
IDENTITIES = {'a': ('COM3', 'E0:72:A1:F7:FF:0C'),
              'b': ('COM5', 'E0:72:A1:D8:3E:74')}


def connect(board):
    port, identity = IDENTITIES[board]
    assert esptool.__version__ == '5.3.0'
    assert any(p.device == port and p.serial_number == identity and
               p.vid == 0x303A and p.pid == 0x1001 for p in list_ports.comports())
    esp = detect_chip(port, baud=115200, connect_mode='usb-reset', connect_attempts=3)
    try:
        assert esp.CHIP_NAME == 'ESP32-S3'
        assert bytes(esp.read_mac()).hex() == identity.replace(':', '').lower()
        attach_flash(esp)
        assert esp.flash_id() == 0x1740C8
        esp.flash_set_parameters(0x800000)
        return esp
    except BaseException:
        esp._port.close()
        raise


def backup(board):
    path = EVIDENCE / f'secure-20260908-before-{board}.bin'
    data = path.read_bytes()
    record = json.loads(path.with_suffix('.json').read_text())
    assert record['serial'] == IDENTITIES[board][1]
    assert len(data) == 0x800000 and record['device_md5_verified']
    assert hashlib.sha256(data).hexdigest() == record['sha256']
    return data


def flash(board):
    original = backup(board)
    assert original[0x224000:0x284000] == b'\xff' * 0x60000
    folder = EVIDENCE / f'secure-20260908-{board}-r3'
    manifest = json.loads((folder / 'hashes.json').read_text())
    for name, digest in manifest.items():
        assert Path(name).name == name
        assert hashlib.sha256((folder / name).read_bytes()).hexdigest() == digest
    config = (folder / 'sdkconfig').read_text().splitlines()
    node = 1 if board == 'a' else 2
    assert f'#define CONFIG_NINLIL_NODE_ID {node}' in (folder / 'sdkconfig.h').read_text().splitlines()
    assert f'#define CONFIG_NINLIL_PEER_ID {3-node}' in (folder / 'sdkconfig.h').read_text().splitlines()
    for required in [f'CONFIG_NINLIL_NODE_ID={node}',
                     f'CONFIG_NINLIL_PEER_ID={3-node}',
                     'CONFIG_NINLIL_M1_MODE_SECURE_BENCH=y',
                     'CONFIG_NINLIL_RF_FREQUENCY_HZ=921400000',
                     'CONFIG_NINLIL_RF_REGION="JP"', 'CONFIG_NINLIL_RF_TX_POWER_DBM=-9',
                     'CONFIG_NINLIL_RF_SF=7', 'CONFIG_NINLIL_RF_BW_125=y',
                     'CONFIG_NINLIL_RF_CR_DENOMINATOR=5',
                     'CONFIG_NINLIL_RF_GATE_RX_ACTIVE_HIGH=y',
                     'CONFIG_NINLIL_RF_GATE_POLARITY_CONFIRMED=y']:
        assert required in config, required
    assert 'Ninlil project CI PASS' in (EVIDENCE / 'secure-20260908-local.log').read_text()
    images = [(0, (folder / 'bootloader.bin').read_bytes()),
              (0x8000, (folder / 'partitions.bin').read_bytes()),
              (0x10000, (folder / 'app.bin').read_bytes())]
    esp = connect(board)
    try:
        assert esp.flash_md5sum(0, 0x800000).lower() == hashlib.md5(original).hexdigest()
        esp = esptool.run_stub(esp)
        attach_flash(esp)
        write_flash(esp, images, flash_freq='80m', flash_mode='dio', flash_size='8MB', no_progress=True)
        verify_flash(esp, images, flash_freq='80m', flash_mode='dio', flash_size='8MB')
        print('SECURE_FLASH_VERIFIED', board, manifest['app.bin'], flush=True)
    finally:
        esp._port.close()


def restore(board):
    original = backup(board)
    esp = connect(board)
    try:
        # Preserve actual control/counter evidence before restoring test-only areas.
        for label, start, size in [('control', 0x224000, 0x20000),
                                    ('sessions', 0x244000, 0x40000)]:
            path = EVIDENCE / f'secure-20260908-after-{board}-{label}.bin'
            data = b''.join(esp.read_flash(off, 0x8000)
                            for off in range(start, start + size, 0x8000))
            assert esp.flash_md5sum(start, size).lower() == hashlib.md5(data).hexdigest()
            with path.open('xb') as output:
                output.write(data)
        images = [(0, original[:0x200000]),
                  (0x224000, original[0x224000:0x284000])]
        esp = esptool.run_stub(esp)
        attach_flash(esp)
        write_flash(esp, images, flash_freq='80m', flash_mode='dio', flash_size='8MB', no_progress=True)
        verify_flash(esp, images, flash_freq='80m', flash_mode='dio', flash_size='8MB')
        assert esp.flash_md5sum(0, 0x800000).lower() == hashlib.md5(original).hexdigest()
        print('ORIGINAL_FULL_FLASH_RESTORED', board, hashlib.sha256(original).hexdigest(), flush=True)
        esp.hard_reset()
    finally:
        esp._port.close()


if __name__ == '__main__':
    action, board = sys.argv[1:]
    assert board in IDENTITIES
    assert action in ('flash', 'restore')
    (flash if action == 'flash' else restore)(board)
