"""Independent ES256 issuer and USB installer for generic Ninlil deployments.

Use an externally managed P-256 PEM private key, separate from radio devices.
Keep the key and allocation database in protected persistent storage. The DB
reserves each generation before publishing its credential; never roll it back.
No firmware/device private key extraction or network service is required.
"""
import argparse
from contextlib import closing
import json
from pathlib import Path
import sqlite3
import struct

from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec, utils
from manage import Device, checked_member, identity, make_member, status, upload


def public_key(key):
    return key.public_key().public_bytes(serialization.Encoding.X962,
                                         serialization.PublicFormat.UncompressedPoint)


def sign(key, member):
    checked_member(member)
    body = b'\x84\x6aSignature1\x43\xa1\x01\x26\x40\x58' + bytes((len(member),)) + member
    r, s = utils.decode_dss_signature(key.sign(body, ec.ECDSA(hashes.SHA256())))
    return (b'\xd2\x84\x43\xa1\x01\x26\xa0\x58' + bytes((len(member),)) + member
            + b'\x58\x40' + r.to_bytes(32, 'big') + s.to_bytes(32, 'big'))


def verify(public, credential):
    if (len(credential) < 235 or len(credential) > 299
            or credential[:8] != b'\xd2\x84\x43\xa1\x01\x26\xa0\x58'
            or len(credential) != credential[8] + 75
            or credential[-66:-64] != b'\x58\x40'):
        raise ValueError('Invalid COSE credential')
    member = credential[9:-66]
    checked_member(member)
    ec.EllipticCurvePublicKey.from_encoded_point(ec.SECP256R1(), member[3:68])
    body = b'\x84\x6aSignature1\x43\xa1\x01\x26\x40\x58' + bytes((len(member),)) + member
    signature = utils.encode_dss_signature(int.from_bytes(credential[-64:-32], 'big'),
                                           int.from_bytes(credential[-32:], 'big'))
    ec.EllipticCurvePublicKey.from_encoded_point(ec.SECP256R1(), public).verify(
        signature, body, ec.ECDSA(hashes.SHA256()))
    return member


def issue(database, key, network, public, address, role, expected, binding_floor=1):
    if (not isinstance(key, ec.EllipticCurvePrivateKey) or not isinstance(key.curve, ec.SECP256R1)
            or len(network) != 16 or not any(network) or expected < 0
            or not 1 <= binding_floor < 2**63):
        raise ValueError('Invalid issuer configuration')
    # SQLite provides cross-process exclusion and durable commit without a daemon.
    with closing(sqlite3.connect(database, timeout=5)) as db, db:
        db.execute('PRAGMA synchronous=FULL')
        db.execute('CREATE TABLE IF NOT EXISTS authority (singleton INTEGER PRIMARY KEY CHECK(singleton=1), network BLOB NOT NULL, public BLOB NOT NULL)')
        db.execute('CREATE TABLE IF NOT EXISTS allocation (address INTEGER PRIMARY KEY, epoch INTEGER NOT NULL, identity BLOB NOT NULL, binding INTEGER NOT NULL, credential BLOB NOT NULL)')
        db.execute('BEGIN IMMEDIATE')
        trust = db.execute('SELECT network, public FROM authority').fetchone()
        if trust is None:
            db.execute('INSERT INTO authority VALUES (1, ?, ?)', (network, public_key(key)))
        elif trust != (network, public_key(key)):
            raise ValueError('Database belongs to another network or issuer')
        old = db.execute('SELECT epoch, binding FROM allocation WHERE address=?', (address,)).fetchone()
        if (old[0] if old else 0) != expected:
            raise ValueError('Stale address generation; no credential issued')
        epoch = expected + 1
        if epoch >= 2**63 or (role == 4 and epoch > 65535):
            raise ValueError('Generation exhausted')
        prior_binding = db.execute('SELECT MAX(binding) FROM allocation WHERE identity=?', (public[:32],)).fetchone()[0] or 0
        binding = max(binding_floor, prior_binding + 1, old[1] + 1 if old else 1)
        if binding >= 2**63:
            raise ValueError('Binding exhausted')
        credential = sign(key, make_member(public, network, address, role, epoch, binding))
        db.execute('INSERT OR REPLACE INTO allocation VALUES (?, ?, ?, ?, ?)',
                   (address, epoch, public[:32], binding, credential))
        db.commit()
    return credential


def install(device, ca, root, own, autorun=False, transfer=False):
    root_member = verify(ca, root)
    local = verify(ca, own) if own else root_member
    info = checked_member(root_member)
    if info['role'] != 4 or info['epoch'] > 65535:
        raise ValueError('Invalid Root credential')
    current_public = identity(device)
    if local[72:104] != current_public[:32] or local[3:68] != current_public[32:]:
        raise ValueError('Credential belongs to another physical device')
    device.call('X')
    revision = status(device)[0]
    packet = struct.pack('>QB', revision, int(autorun)) + ca + struct.pack('>H', len(root)) + root + own
    upload(device, 5 if transfer else 4, packet)
    result = status(device)
    if result[:2] != (revision + 1, autorun):
        raise RuntimeError('Configuration publication not verified')
    if autorun:
        device.call('G', bytes(4), timeout=30)
    return {'revision': result[0], 'autorun': result[1], 'address': result[2],
            'root': result[3], 'delivery_verified': False}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    actions = parser.add_subparsers(dest='action', required=True)
    issuance = actions.add_parser('issue')
    issuance.add_argument('--key', type=Path, required=True, help='Existing P-256 PKCS8 PEM; never printed')
    issuance.add_argument('--database', type=Path, required=True)
    issuance.add_argument('--network', required=True, help='Public 16-byte network ID, hex')
    issuance.add_argument('--public', required=True, help='Public I response, 97 bytes hex')
    issuance.add_argument('--address', type=int, required=True)
    issuance.add_argument('--role', choices=('root', 'relay', 'endpoint', 'battery'), required=True)
    issuance.add_argument('--expected-epoch', type=int, required=True)
    issuance.add_argument('--binding-floor', type=int, default=1)
    issuance.add_argument('--output', type=Path, required=True)
    installation = actions.add_parser('install')
    installation.add_argument('--port', required=True)
    installation.add_argument('--ca-public', required=True, help='Public uncompressed P-256 point, hex')
    installation.add_argument('--root', type=Path, required=True)
    installation.add_argument('--credential', type=Path)
    installation.add_argument('--transfer', action='store_true')
    installation.add_argument('--autorun', action='store_true')
    args = parser.parse_args()
    if args.action == 'issue':
        key = serialization.load_pem_private_key(args.key.read_bytes(), password=None)
        role = {'root': 4, 'relay': 3, 'endpoint': 2, 'battery': 1}[args.role]
        credential = issue(args.database, key, bytes.fromhex(args.network), bytes.fromhex(args.public),
                           args.address, role, args.expected_epoch, args.binding_floor)
        # This is a public signed credential, recoverable from the allocation DB.
        with args.output.open('xb') as output:
            output.write(credential)
            output.flush()
        print(json.dumps({'ca_public': public_key(key).hex(), **checked_member(verify(public_key(key), credential))}))
    else:
        device = Device(args.port)
        try:
            print(json.dumps(install(device, bytes.fromhex(args.ca_public), args.root.read_bytes(),
                                      args.credential.read_bytes() if args.credential else b'',
                                      args.autorun, args.transfer)))
        finally:
            device.close()


if __name__ == '__main__':
    main()
