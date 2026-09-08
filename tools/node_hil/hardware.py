"""Identity-checked additive flashing; hashes only, no device backups/key reads."""
import argparse
import hashlib
import json
from pathlib import Path

import esptool
from esptool.cmds import attach_flash, detect_chip, verify_flash, write_flash
from serial.tools import list_ports

IDENTITIES = {1: 'E0:72:A1:F7:FF:0C', 2: 'E0:72:A1:D8:3E:74',
              3: 'E0:72:A1:D7:77:28'}
PROTECTED = {'nvs_phy': (0x9000, 0x7000),
             'prior_stores': (0x190000, 0x424000),
             'existing_node_stores': (0x620000, 0x17c000)}
PARTITION_SHA256 = 'f46dace73eaa0a69d56f4ceae9db6fa72f22086c4db88c9e8fb961ec52d48a1a'


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def connect(node):
    ports = [p for p in list_ports.comports() if p.vid == 0x303a and
             p.pid == 0x1001 and p.serial_number == IDENTITIES[node]]
    if len(ports) != 1 or esptool.__version__ != '5.3.0':
        raise RuntimeError('Expected verified board and esptool 5.3.0')
    esp = detect_chip(ports[0].device, baud=115200, connect_mode='usb-reset',
                      connect_attempts=3)
    try:
        require(esp.CHIP_NAME == 'ESP32-S3', 'Unexpected chip')
        require(bytes(esp.read_mac()).hex() == IDENTITIES[node].replace(':', '').lower(), 'ROM identity mismatch')
        attach_flash(esp)
        require(esp.flash_id() == 0x1740c8, 'Unverified flash chip')
        esp.flash_set_parameters(0x800000)
        return esp
    except BaseException:
        esp._port.close()
        raise


def hashes(esp):
    return {name: esp.flash_md5sum(start, size).lower()
            for name, (start, size) in PROTECTED.items()}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('node', type=int, choices=IDENTITIES)
    p.add_argument('folder', type=Path)
    p.add_argument('--initial', action='store_true',
                   help='Require all additive node storage to be erased')
    args = p.parse_args()
    folder = args.folder.resolve()
    manifest = json.loads((folder / 'hashes.json').read_text())
    require(isinstance(manifest, dict) and set(manifest) == {
        'app.bin', 'app.elf', 'bootloader.bin', 'partitions.bin', 'sdkconfig', 'sdkconfig.h'},
        'Incomplete or unexpected build manifest')
    for name, digest in manifest.items():
        require(Path(name).name == name, 'Invalid artifact filename')
        require(hashlib.sha256((folder / name).read_bytes()).hexdigest() == digest, 'Artifact hash mismatch: '+name)
    require(manifest['partitions.bin'] == PARTITION_SHA256,
            'Partition binary differs from the verified additive fixture map')
    config = (folder / 'sdkconfig').read_text().splitlines()
    require(f'CONFIG_NINLIL_NODE_ID={args.node}' in config, 'Firmware node mismatch')
    require('CONFIG_NINLIL_MODE_AUTONOMOUS=y' in config, 'Wrong firmware mode')
    require('CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions-autonomous-preserve.csv"' in config, 'Unverified partition layout')
    images = [(0, (folder / 'bootloader.bin').read_bytes()),
              (0x8000, (folder / 'partitions.bin').read_bytes()),
              (0x10000, (folder / 'app.bin').read_bytes())]
    require(0 < len(images[0][1]) <= 0x8000 and 0 < len(images[1][1]) <= 0x1000, 'Boot image exceeds reserved space')
    require(0 < len(images[2][1]) <= 0x180000, 'Application exceeds its partition')
    esp = connect(args.node)
    try:
        before = hashes(esp)
        if args.initial:
            start, size = PROTECTED['existing_node_stores']
            require(esp.flash_md5sum(start, size).lower() == hashlib.md5(
                b'\xff' * size).hexdigest(), 'New storage is not erased; preserve and inspect')
        esp = esptool.run_stub(esp)
        attach_flash(esp)
        write_flash(esp, images, flash_freq='80m', flash_mode='dio',
                    flash_size='8MB', no_progress=True)
        verify_flash(esp, images, flash_freq='80m', flash_mode='dio', flash_size='8MB')
        after = hashes(esp)
        record = {'serial': IDENTITIES[args.node], 'app_sha256': manifest['app.bin'],
                  'before': before, 'after': after, 'preserved': before == after,
                  'device_image_verified': True, 'backups_created': False}
        (folder / 'device-verification.json').write_text(json.dumps(record, indent=2)+'\n')
        require(before == after, 'Protected storage changed')
        print('VERIFIED_ADDITIVE_IMAGE', args.node, manifest['app.bin'], flush=True)
        esp.hard_reset()
    finally:
        esp._port.close()


if __name__ == '__main__':
    main()
