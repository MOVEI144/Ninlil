import hashlib
import json
import os
import re
import sys
from datetime import datetime, timezone
from pathlib import Path
import esptool
from esptool.cmds import attach_flash, detect_chip, erase_region
from serial.tools import list_ports

root = Path(__file__).resolve().parents[2] / ".verify-m1-evidence"
board, label, action, variant = sys.argv[1:5]
assert variant in ('fault-a-init', 'fault-a-resp', 'fault-b-init', 'fault-b-resp')
assert variant.split('-')[1] == board
assert action in ('read', 'clean') and re.fullmatch(r'[a-z0-9-]+', label)
identities = {'a': ('COM3', 'E0:72:A1:F7:FF:0C'), 'b': ('COM5', 'E0:72:A1:D8:3E:74')}
port_name, identity = identities[board]
devices = [p for p in list_ports.comports() if p.device == port_name]
assert len(devices) == 1 and (devices[0].serial_number, devices[0].vid, devices[0].pid) == (identity, 0x303A, 0x1001)
assert esptool.__version__ == '5.3.0'
target = root / f'{label}-{board}-journal.bin'
partial = target.with_suffix('.partial')
assert not target.exists() and not partial.exists()
esp = detect_chip(port_name, baud=115200, connect_mode='usb-reset', connect_attempts=3)
try:
    assert bytes(esp.read_mac()).hex() == identity.replace(':', '').lower()
    assert esp.CHIP_NAME == 'ESP32-S3' and not esp.IS_STUB
    attach_flash(esp)
    assert esp.flash_id() == 0x1740C8
    esp.flash_set_parameters(0x800000)
    table = (root / 'init-a-fix-build/partition-table.bin').read_bytes()
    assert hashlib.sha256(table).hexdigest() == '59514879595ffbb48032cd5ea36f6d7b27479f00b35304ec1a934fef64565a47'
    assert esp.read_flash(0x8000, len(table)) == table
    firmware = (root / (variant + '-r1') / 'ninlil_m1.bin').read_bytes()
    assert esp.flash_md5sum(0x10000, len(firmware)).lower() == hashlib.md5(firmware).hexdigest()
    start, size = 0x200000, 0x20000
    data = b''
    with partial.open('xb') as output:
        for offset in range(0, size, 0x8000):
            block = esp.read_flash(start + offset, 0x8000)
            assert len(block) == 0x8000
            data += block
            output.write(block)
        output.flush()
        os.fsync(output.fileno())
    assert len(data) == size
    assert esp.flash_md5sum(start, size).lower() == hashlib.md5(data).hexdigest()
    assert partial.read_bytes() == data
    partial.rename(target)
    erased = data == b'\xff' * size
    result = {'board': board, 'serial': identity, 'action': action,
        'offset': start, 'bytes': size, 'sha256': hashlib.sha256(data).hexdigest(),
        'app_sha256': hashlib.sha256(firmware).hexdigest(),
        'app_device_md5_verified': True, 'md5_device_verified': True, 'initially_erased': erased}
    if action == 'clean':
        if not erased:
            # Preserve verified readback first; erase only the known test journal.
            esp = esptool.run_stub(esp)
            attach_flash(esp)
            erase_region(esp, start, size)
        assert esp.flash_md5sum(start, size).lower() == hashlib.md5(b'\xff' * size).hexdigest()
        result['clean_verified'] = True
    result['utc'] = datetime.now(timezone.utc).isoformat()
    with (root / f'{label}-{board}-journal.json').open('x') as output:
        json.dump(result, output, indent=2)
    print(json.dumps(result, indent=2), flush=True)
finally:
    esp._port.close()
