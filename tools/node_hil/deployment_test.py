"""Exercise USB configuration persistence on the three identified bench boards.

Migrates the existing network without clearing history. Uses software reset,
not an electrical write-time power cut. Leaves all devices stopped/disarmed.
"""
import argparse
import hashlib
import json
import struct
import time
from pathlib import Path

from campaign import status as runtime_status
from console import Board
from hardware import require
from manage import apply, identity, member, status, upload


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('evidence', type=Path)
    args = parser.parse_args()
    args.evidence.mkdir(parents=True, exist_ok=False)
    boards, result = [], {'scope': __doc__, 'started': time.time(), 'nodes': []}
    with (args.evidence / 'console.jsonl').open('x', encoding='utf-8') as log:
        try:
            for i in (1, 2, 3):
                boards.append(Board(i, log))
            for b in boards:
                b.call('X')
            for index, b in enumerate(boards):
                # The example ledger is mounted only while its owner runs.
                b.call('G', (30000).to_bytes(4, 'big'), timeout=30)
                before = runtime_status(b)
                b.call('X', timeout=30)
                public = identity(b)
                _, _, local, root_id, _ = status(b)
                root, own = member(b, root_id), member(b, local)
                require(member(boards[0], root_id) == root, 'Existing Root differs')
                credential = b'' if local == root_id else upload(boards[0], 2, own)
                row = {'node': index + 1, 'before': before,
                       'identity_sha256': hashlib.sha256(public).hexdigest()}
                result['nodes'].append(row)
                revision = status(b)[0]
                row['configured'] = apply(b, root, credential, False)
                # A lost response may repeat the exact committed request.
                blob = struct.pack('>QBH', revision, 0, len(root)) + root + credential
                upload(b, 1, blob)
                require(status(b)[0] == revision + 1, 'Duplicate changed setup revision')
                # A different request using that stale revision must fail.
                changed = blob[:8] + b'\x01' + blob[9:]
                b.call('U', b'\x00\x01' + struct.pack('>H', len(changed)))
                for offset in range(0, len(changed), 120):
                    b.call('U', b'\x01' + struct.pack('>H', offset) + changed[offset:offset+120])
                rc, _ = b.call('U', b'\x02', expected=None, timeout=30)
                require(rc == -5, 'Stale differing setup must return CONFLICT')
                row['idempotence_and_stale_conflict'] = True
                row['armed'] = apply(b, root, credential, True)
                require(runtime_status(b)['running'], 'Armed runtime did not start')
                b.call('R')
                b.close()
                deadline = time.monotonic() + 30
                while True:
                    try:
                        boards[index] = b = Board(index + 1, log)
                        after = runtime_status(b)
                        require(after['running'] and not after['fault'], 'Autorun did not recover')
                        break
                    except (OSError, RuntimeError, TimeoutError):
                        b.close()
                        if time.monotonic() >= deadline:
                            raise
                        time.sleep(0.5)
                require(identity(b) == public, 'Configuration changed device identity')
                require(status(b)[0] == row['armed']['configuration_revision'] and status(b)[1],
                        'Reset changed saved configuration')
                require(after['application_records'] == before['application_records'],
                        'Configuration/reset changed application ledger')
                row['after_software_reset'] = after
                b.call('X', timeout=30)
                require(not status(b)[1] and not runtime_status(b)['running'], 'Stop did not persist disarm')
                row['stopped_and_disarmed'] = True
            result['result'] = 'PASS'
        except BaseException as error:
            result['result'], result['error'] = 'FAIL', repr(error)
            raise
        finally:
            for b in boards:
                try:
                    b.call('X', timeout=30)
                except Exception as error:
                    result.setdefault('stop_errors', []).append(repr(error))
                finally:
                    b.close()
            if result.get('stop_errors'):
                result['result'] = 'FAIL'
            result['finished'] = time.time()
            (args.evidence / 'result.json').write_text(json.dumps(result, indent=2)+'\n', encoding='utf-8')
            require(not result.get('stop_errors'), 'Device cleanup failed')
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
