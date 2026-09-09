"""Configure the reference firmware over physical USB without reflashing.

USB access is the trusted local management boundary. Public IDs, keys and
signed grants only; private keys remain on the device. Radio profile stays in
the validated firmware build. This tool reports configuration, not delivery.
"""
import argparse
import hashlib
import json
import secrets
import struct

import serial
from serial.tools import list_ports
from console import Board


class Device(Board):
    def __init__(self, port):
        matches = [p for p in list_ports.comports() if p.device == port
                   and p.vid == 0x303a and p.pid == 0x1001]
        if len(matches) != 1:
            raise ValueError('Expected one ESP32-S3 USB device at the explicit port')
        self.node, self.log = matches[0].serial_number, None
        self.port = serial.Serial(port=None, baudrate=115200, timeout=0.1)
        self.port.dtr = self.port.rts = False
        self.port.port = port
        self.port.open()


def status(device):
    data = device.call('U', b'\x04')
    if len(data) != 15 or data[8] > 1:
        raise ValueError('Invalid setup status')
    revision, autorun, local, root, length = struct.unpack('>QBHHH', data)
    if length > 299:
        raise ValueError('Invalid public response length')
    return revision, bool(autorun), local, root, length


def control_status(device):
    result = {}
    for kind in (6, 7, 10, 11, 12, 13, 14, 15, 16, 17, 18, 26, 28, 31):
        data = device.call('U', bytes((6, kind)))
        if len(data) != 8:
            raise ValueError('Invalid control counters')
        received, accepted, last = struct.unpack('>HHi', data)
        if accepted > received:
            raise ValueError('Control counters do not reconcile')
        result[kind] = {'received': received, 'accepted': accepted, 'last_result': last}
    return result


def response(device):
    length = status(device)[4]
    data = b''
    while len(data) < length:
        chunk = device.call('U', b'\x03' + struct.pack('>H', len(data)))
        if not chunk or len(chunk) > min(120, length - len(data)):
            raise ValueError('Invalid response chunk')
        data += chunk
    return data


def upload(device, operation, blob):
    if operation not in (1, 2, 3, 4, 5) or not 0 < len(blob) <= 674:
        raise ValueError('Invalid management upload')
    device.call('U', bytes((0, operation)) + struct.pack('>H', len(blob)))
    for offset in range(0, len(blob), 120):
        device.call('U', b'\x01' + struct.pack('>H', offset) + blob[offset:offset + 120])
    device.call('U', b'\x02', timeout=30)
    return response(device)


def checked_member(blob):
    if (len(blob) < 160 or len(blob) > 224 or blob[:4] != b'NM\x01\x04'
            or blob[68:72] != b'NJ\x01\x01' or blob[143] > 8
            or len(blob) != 160 + 8 * blob[143]
            or blob[144:160] != b'\x01' + bytes(15)):
        raise ValueError('Invalid public member record')
    node, epoch, binding, capabilities, role, count = struct.unpack('>HQQIBB', blob[120:144])
    if (not 0 < node < 65535 or not epoch or not binding or capabilities & ~63
            or role not in (1, 2, 3, 4) or not any(blob[72:104]) or not any(blob[104:120])):
        raise ValueError('Invalid member fields')
    return {'node': node, 'epoch': epoch, 'binding': binding, 'role': role,
            'identity': blob[72:104].hex(), 'authority': blob[104:120].hex()}


def member(device, address):
    device.call('U', b'\x05' + struct.pack('>H', address))
    blob = response(device)
    info = checked_member(blob)
    if info['node'] != address:
        raise ValueError('Member address mismatch')
    return blob


def identity(device):
    data = device.call('I')
    if len(data) != 97 or data[32] != 4 or not any(data[:32]):
        raise ValueError('Invalid public identity; provision explicitly first')
    return data


def make_member(public, authority, address, role, epoch=1, binding=1):
    if len(public) != 97 or public[32] != 4 or len(authority) != 16:
        raise ValueError('Invalid public identity or authority')
    capabilities = 3 | (16 if role == 3 else 0) | (4 if role == 1 else 0)
    blob = (b'NM\x01' + public[32:] + b'NJ\x01\x01' + public[:32] + authority
            + struct.pack('>HQQIBB', address, epoch, binding, capabilities, role, 1)
            + b'\x01' + bytes(15) + struct.pack('>HHHBB', 256, 64, 16, 3, 15))
    checked_member(blob)
    return blob


def apply(device, root, credential, autorun):
    device.call('X')
    revision = status(device)[0]
    upload(device, 1, struct.pack('>QBH', revision, int(autorun), len(root)) + root + credential)
    current = status(device)
    if current[0] != revision + 1 or current[1] != autorun:
        raise RuntimeError('Stored setup does not match the requested revision')
    if autorun:
        device.call('G', bytes(4), timeout=30)
    return {'configuration_revision': current[0], 'autorun': current[1],
            'local_address': current[2], 'root_address': current[3],
            'firmware_written': False, 'delivery_verified': False}


def authorize_for(device, root_device, root, own):
    # Reject unsupported device changes before mutating Root's live registry.
    _, _, local, root_id, _ = status(device)
    proposed = checked_member(own)
    if local:
        previous = checked_member(member(device, local))
        if (member(device, root_id) != root or proposed['node'] != local
                or any(proposed[key] != previous[key] for key in ('identity', 'authority', 'role'))
                or proposed['epoch'] < previous['epoch'] or proposed['binding'] < previous['binding']):
            raise ValueError('Existing device requires the same Root, address, identity, site and role; no authorization changed')
    return upload(root_device, 2, own)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=('info', 'provision', 'root', 'adopt', 'enroll'))
    parser.add_argument('--port', required=True)
    parser.add_argument('--root-port')
    parser.add_argument('--address', type=int)
    parser.add_argument('--role', choices=('endpoint', 'relay', 'battery'), default='endpoint')
    parser.add_argument('--authority', help='16-byte public network ID in hexadecimal; new Root only')
    parser.add_argument('--epoch', type=int, default=1)
    parser.add_argument('--binding', type=int, default=1)
    parser.add_argument('--autorun', action='store_true')
    args = parser.parse_args()
    device, root_device = Device(args.port), None
    try:
        if args.action == 'provision':
            device.call('P', timeout=30)
            print(json.dumps({'public_identity': identity(device).hex()}))
            return
        if args.action == 'info':
            print(json.dumps({'device': device.node, 'public_identity': identity(device).hex(),
                              'setup': status(device)}))
            return
        public = identity(device)
        if args.action == 'root':
            authority = bytes.fromhex(args.authority) if args.authority else secrets.token_bytes(16)
            root = make_member(public, authority, args.address or 1, 4)
            credential = b''
        elif args.action == 'adopt':
            _, _, local, root_id, _ = status(device)
            root, own = member(device, root_id), member(device, local)
            if own[72:104] != public[:32] or own[3:68] != public[32:]:
                raise ValueError('Existing setup belongs to another device')
            credential = b''
            if local != root_id:
                if not args.root_port or args.root_port == args.port:
                    raise ValueError('Child adoption requires its existing Root port')
                root_device = Device(args.root_port)
                if member(root_device, root_id) != root:
                    raise ValueError('Different Root identity or grant')
                credential = upload(root_device, 2, own)
        else:
            if not args.root_port or args.root_port == args.port or args.address is None:
                raise ValueError('Enrollment requires a distinct Root port and a node address')
            root_device = Device(args.root_port)
            root = member(root_device, status(root_device)[3])
            role = {'endpoint': 2, 'relay': 3, 'battery': 1}[args.role]
            own = make_member(public, root[104:120], args.address, role, args.epoch, args.binding)
            credential = authorize_for(device, root_device, root, own)
        result = apply(device, root, credential, args.autorun)
        result['authority'] = checked_member(root)['authority']
        result['credential_sha256'] = hashlib.sha256(credential).hexdigest() if credential else None
        print(json.dumps(result, indent=2))
    finally:
        if root_device:
            root_device.close()
        device.close()


if __name__ == '__main__':
    main()
