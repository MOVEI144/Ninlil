"""Bounded M1 bench test: corrupt one committed byte, then restore in finally.

Uses the already verified recovery firmware and journal backup for board B.
This is a deliberate stored-data corruption test, not a Flash power-cut test.
Board A must remain in the ROM loader throughout this test.
"""

import hashlib
import json
import sys
import time
from datetime import datetime, timezone
from pathlib import Path

import esptool
from esptool.cmds import attach_flash, detect_chip, verify_flash, write_flash
from esptool.reset import USBJTAGSerialReset
from serial.tools import list_ports


def main():
    root = Path(sys.argv[1]).resolve()
    backup = root / 'all-feasible-before-b-b-journal.bin'
    meta = json.loads(backup.with_suffix('.json').read_text())
    original = backup.read_bytes()
    assert len(original) == 0x20000 and meta['md5_device_verified']
    assert hashlib.sha256(original).hexdigest() == meta['sha256']
    assert meta['serial'] == 'E0:72:A1:D8:3E:74'
    assert esptool.__version__ == '5.3.0'
    device = [p for p in list_ports.comports() if p.device == 'COM5']
    assert len(device) == 1
    assert (device[0].serial_number, device[0].vid, device[0].pid) == (
        meta['serial'], 0x303A, 0x1001)
    # First IN_ACCEPT body starts at 32, its first payload byte at 32 + 36.
    # Preserve the checksum, commit marker and every other byte.
    assert original[32] == 5
    damaged = bytearray(original[:4096])
    damaged[68] ^= 1
    prefix = root / 'all-feasible-corruption'
    assert not prefix.with_suffix('.json').exists()
    assert not prefix.with_suffix('.log').exists()
    result = {'begin_utc': datetime.now(timezone.utc).isoformat(),
              'serial': meta['serial'], 'journal_backup_sha256': meta['sha256'],
              'sector_address': 0x200000, 'changed_journal_byte': 68,
              'damaged_sector_sha256': hashlib.sha256(damaged).hexdigest(),
              'restored': False, 'passed': False}
    esp = detect_chip('COM5', baud=115200, connect_mode='no-reset', connect_attempts=3)
    modified = False
    try:
        assert esp.CHIP_NAME == 'ESP32-S3'
        assert bytes(esp.read_mac()).hex() == 'e072a1d83e74'
        attach_flash(esp)
        assert esp.flash_id() == 0x1740C8
        esp.flash_set_parameters(0x800000)
        assert esp.flash_md5sum(0x200000, len(original)).lower() == hashlib.md5(original).hexdigest()
        firmware = (root / 'recovery-b-resp-r1/ninlil_m1.bin').read_bytes()
        assert hashlib.sha256(firmware).hexdigest() == '1727aa1017decb2bf33f090bee37d189caf662a2970a5db073d1af26614efa66'
        assert esp.flash_md5sum(0x10000, len(firmware)).lower() == hashlib.md5(firmware).hexdigest()
        table = (root / 'recovery-b-resp-r1/partition-table.bin').read_bytes()
        assert hashlib.sha256(table).hexdigest() == '59514879595ffbb48032cd5ea36f6d7b27479f00b35304ec1a934fef64565a47'
        assert esp.read_flash(0x8000, len(table)) == table
        esp = esptool.run_stub(esp)
        modified = True
        write_flash(esp, [(0x200000, bytes(damaged))], no_progress=True)
        verify_flash(esp, [(0x200000, bytes(damaged))])
        port = esp._port
        port.timeout = 0.2
        port.reset_input_buffer()
        esp.hard_reset()
        raw = b''
        deadline = time.monotonic() + 20
        with prefix.with_suffix('.log').open('xb') as output:
            while time.monotonic() < deadline:
                data = port.read(min(max(port.in_waiting, 1), 4096))
                raw += data
                assert len(raw) <= 1048576
                output.write(data)
                output.flush()
                if b'Returned from app_main()' in raw:
                    break
        result['boot_log_sha256'] = hashlib.sha256(raw).hexdigest()
        assert b'App version:      80ab9ac' in raw
        assert b'durable Runtime open failed' in raw
        assert b'Returned from app_main()' in raw
        for forbidden in (b'NINLIL_HIL_DELIVERY_READY', b'HIL_LINK_SENT', b'HIL_STORED', b'HIL_CONSUMED', b'Guru Meditation'):
            assert forbidden not in raw
        result['passed'] = True
    finally:
        esp._port.close()
        if modified:
            esp = detect_chip('COM5', baud=115200, connect_mode='usb-reset', connect_attempts=3)
            try:
                assert bytes(esp.read_mac()).hex() == 'e072a1d83e74'
                esp = esptool.run_stub(esp)
                attach_flash(esp)
                write_flash(esp, [(0x200000, original[:4096])], no_progress=True)
                verify_flash(esp, [(0x200000, original[:4096])])
                assert esp.flash_md5sum(0x200000, len(original)).lower() == hashlib.md5(original).hexdigest()
                result['restored'] = True
                USBJTAGSerialReset(esp._port)()
            finally:
                esp._port.close()
        result['end_utc'] = datetime.now(timezone.utc).isoformat()
        prefix.with_suffix('.json').write_text(json.dumps(result, indent=2) + '\n')
        print(json.dumps(result, indent=2), flush=True)
    assert result['passed'] and result['restored']


if __name__ == '__main__':
    main()
