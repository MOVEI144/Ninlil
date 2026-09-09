"""USB management preflight: rejected settings cannot revoke a working grant."""
import sys
import unittest
from pathlib import Path
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools/node_hil'))
import manage


class EnrollmentPreflight(unittest.TestCase):
    def test_unconfigured_device_can_be_authorized(self):
        public = bytes([3]) * 32 + b'\x04' + bytes([4]) * 64
        own = manage.make_member(public, bytes([7]) * 16, 3, 2)
        device, authority = object(), object()
        with patch.object(manage, 'status', return_value=(0, False, 0, 0, 0)), \
                patch.object(manage, 'member') as member, \
                patch.object(manage, 'upload', return_value=b'credential') as upload:
            self.assertEqual(manage.authorize_for(device, authority, b'', own), b'credential')
            member.assert_not_called()
            upload.assert_called_once_with(authority, 2, own)

    def test_before_authority_mutation(self):
        public = bytes([3]) * 32 + b'\x04' + bytes([4]) * 64
        network = bytes([7]) * 16
        root = manage.make_member(public, network, 1, 4)
        old = manage.make_member(public, network, 3, 2, 2, 2)
        device, authority = object(), object()
        with patch.object(manage, 'status', return_value=(4, False, 3, 1, 0)), \
                patch.object(manage, 'member', side_effect=lambda _, address: old if address == 3 else root), \
                patch.object(manage, 'upload', return_value=b'credential') as upload:
            rejected = [(root, b'bad'), (root[:-1] + b'\0', old)]
            for address, role, epoch, binding in ((4, 2, 3, 2), (3, 1, 3, 2),
                                                  (3, 2, 1, 2), (3, 2, 3, 1)):
                rejected.append((root, manage.make_member(public, network, address, role, epoch, binding)))
            rejected.append((root, manage.make_member(bytes([9]) + public[1:], network, 3, 2, 3, 2)))
            for candidate_root, candidate in rejected:
                with self.assertRaises(ValueError):
                    manage.authorize_for(device, authority, candidate_root, candidate)
                upload.assert_not_called()
            newer = manage.make_member(public, network, 3, 2, 3, 2)
            self.assertEqual(manage.authorize_for(device, authority, root, newer), b'credential')
            upload.assert_called_once_with(authority, 2, newer)


if __name__ == '__main__':
    unittest.main()
