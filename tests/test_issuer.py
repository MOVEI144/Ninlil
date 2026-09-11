"""Real ES256 signatures and durable allocation/replay, without device secrets."""
import sqlite3
from contextlib import closing
import sys
import tempfile
import unittest
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools/node_hil'))
import issuer
from cryptography.exceptions import InvalidSignature
from cryptography.hazmat.primitives.asymmetric import ec


class IssuerTests(unittest.TestCase):
    def test_replacement_reuse_and_restart(self):
        key = ec.generate_private_key(ec.SECP256R1())
        public = bytes([7]) * 32 + issuer.public_key(ec.generate_private_key(ec.SECP256R1()))
        network = bytes([1]) * 16
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / 'allocation.sqlite'
            first = issuer.issue(path, key, network, public, 1, 4, 0)
            self.assertEqual(issuer.checked_member(issuer.verify(issuer.public_key(key), first))['epoch'], 1)
            with self.assertRaises(ValueError):
                issuer.issue(path, key, network, public, 1, 4, 0)
            replacement = bytes([9]) * 32 + issuer.public_key(ec.generate_private_key(ec.SECP256R1()))
            second = issuer.issue(path, key, network, replacement, 1, 4, 1)
            info = issuer.checked_member(issuer.verify(issuer.public_key(key), second))
            self.assertEqual((info['epoch'], info['binding']), (2, 2))
            with closing(sqlite3.connect(path)) as db:
                self.assertEqual(db.execute('SELECT credential FROM allocation').fetchone()[0], second)
            with self.assertRaises(InvalidSignature):
                issuer.verify(issuer.public_key(key), second[:-1] + bytes([second[-1] ^ 1]))
            with self.assertRaises(ValueError):
                issuer.issue(path, key, bytes([2]) * 16, public, 2, 2, 0)
            third = issuer.issue(path, key, network, public, 2, 1, 0, 8)
            self.assertEqual(issuer.checked_member(issuer.verify(issuer.public_key(key), third))['binding'], 8)


if __name__ == '__main__':
    unittest.main()
