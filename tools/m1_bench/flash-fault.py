import hashlib
import sys
from datetime import datetime, timezone
from pathlib import Path

import esptool
from esptool.cmds import attach_flash, detect_chip, verify_flash, write_flash
from serial.tools import list_ports

root = Path(__file__).resolve().parents[2] / ".verify-m1-evidence"
variant = sys.argv[1]
roles = {'fault-a-init': ('a', 1, 2), 'fault-a-resp': ('a', 1, 2),
         'fault-b-init': ('b', 2, 1), 'fault-b-resp': ('b', 2, 1), 'recovery-a-init': ('a', 1, 2), 'recovery-b-resp': ('b', 2, 1)}
board, node, peer = roles[variant]
identities = {
    'a': ('COM3', 'E0:72:A1:F7:FF:0C', '09d9a0d06fc4511146e1c8a74caee5b93a2f35857d4f9c01665a93b0634be2de'),
    'b': ('COM5', 'E0:72:A1:D8:3E:74', '1911ac3d500bbcef6d1c951d1f58a25757ca787f7023f31b7ee97ab93420819a'),
}
port, identity, backup_hash = identities[board]
assert esptool.__version__ == '5.3.0'
assert 'Ninlil project CI PASS' in (root / 'fault-full-local.log').read_text()
assert '100% tests passed' in (root / 'all-feasible-focused.log').read_text()
backup = root / f'board-{board}-before-ninlil-8mb.bin'
assert backup.stat().st_size == 0x800000
assert hashlib.sha256(backup.read_bytes()).hexdigest() == backup_hash
campaign = 2026090703 if variant.startswith('recovery') else (2026090704 if variant in ('fault-a-init', 'fault-b-resp') else 2026090705)
folder = root / (variant + '-r1')
assert (folder / 'source-commit.txt').read_text().strip() == '3254ae3f4f76a51061c2d9a79b1b37eb8804c302'
for line in (folder / 'SHA256SUMS').read_text().splitlines():
    digest, name = line.split()
    assert Path(name).name == name
    assert hashlib.sha256((folder / name).read_bytes()).hexdigest() == digest
config = (folder / 'sdkconfig').read_text().splitlines()
required = [f'CONFIG_NINLIL_NODE_ID={node}', f'CONFIG_NINLIL_PEER_ID={peer}',
    'CONFIG_NINLIL_RF_REGION="JP"', 'CONFIG_NINLIL_RF_FREQUENCY_HZ=921400000',
    'CONFIG_NINLIL_RF_TX_POWER_DBM=-9', 'CONFIG_NINLIL_RF_SF=7',
    'CONFIG_NINLIL_RF_BW_125=y', 'CONFIG_NINLIL_RF_TX_ENABLE=y',
    'CONFIG_NINLIL_RF_GATE_RX_ACTIVE_HIGH=y', 'CONFIG_NINLIL_RF_GATE_POLARITY_CONFIRMED=y',
    'CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y']
required += ['CONFIG_NINLIL_DELIVERY_SUBMIT_ON_BOOT=y'] if variant.endswith('init') else ['# CONFIG_NINLIL_DELIVERY_SUBMIT_ON_BOOT is not set']
required += ['CONFIG_NINLIL_DELIVERY_FAULT_CAMPAIGN=y', 'CONFIG_NINLIL_M1_MODE_DELIVERY=y', 'CONFIG_NINLIL_DELIVERY_MESSAGE_COUNT=100', f'CONFIG_NINLIL_DELIVERY_CAMPAIGN_ID={campaign}']
assert all(line in config for line in required)
devices = [p for p in list_ports.comports() if p.device == port]
assert len(devices) == 1 and (devices[0].serial_number, devices[0].vid, devices[0].pid) == (identity, 0x303A, 0x1001)
print(f'BEGIN variant={variant} utc={datetime.now(timezone.utc).isoformat()} port={port} serial={identity}', flush=True)
esp = detect_chip(port, baud=115200, connect_mode='usb-reset', connect_attempts=3)
try:
    assert esp.CHIP_NAME == 'ESP32-S3'
    assert bytes(esp.read_mac()).hex() == identity.replace(':', '').lower()
    esp = esptool.run_stub(esp)
    attach_flash(esp)
    assert esp.flash_id() == 0x1740C8
    images = [(0, (folder / 'bootloader.bin').read_bytes()),
              (0x8000, (folder / 'partition-table.bin').read_bytes()),
              (0x10000, (folder / 'ninlil_m1.bin').read_bytes())]
    for address, data in images:
        print(f'IMAGE address={address:#x} bytes={len(data)} sha256={hashlib.sha256(data).hexdigest()}', flush=True)
    write_flash(esp, images, flash_freq='80m', flash_mode='dio', flash_size='8MB', no_progress=True)
    verify_flash(esp, images, flash_freq='80m', flash_mode='dio', flash_size='8MB')
    print(f'FLASH_VERIFIED variant={variant} after=no-reset-stub', flush=True)
finally:
    esp._port.close()
