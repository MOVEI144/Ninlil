"""Physical timer sleep/wake and fresh downlink on a configured battery node.

Run after explicit role installation. Never retires custody, changes trust or
erases storage. The exclusive USB console may detach during Light-sleep.
"""
import argparse
import json
from pathlib import Path
import time
from console import Board
from campaign import status


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output', type=Path)
    parser.add_argument('--leaf', type=int, choices=(2, 3), required=True)
    parser.add_argument('--sequence', type=int, required=True)
    parser.add_argument('--restart-window', action='store_true', help='Check restart after the reference 120-second awake window')
    parser.add_argument('--usb-awake', action='store_true', help='Check host-connected awake operation instead of forced sleep')
    args = parser.parse_args()
    if not 0 < args.sequence < 2**32:
        parser.error('Sequence must be a fresh nonzero uint32')
    args.output.mkdir(parents=True, exist_ok=False)
    result = {'scope': 'physical light-sleep and post-wake application delivery',
              'started': time.time(), 'result': 'FAIL', 'snapshots': []}
    boards = []
    with (args.output / 'console.jsonl').open('x', encoding='utf8') as log:
        try:
            for n in (1, args.leaf):
                boards.append(Board(n, log))
            if args.restart_window:
                leaf = boards[1]
                leaf.call('X')
                leaf.call('G', (30000).to_bytes(4, 'big'))
                first = status(leaf)
                leaf.call('X')
                time.sleep(125)  # Expire the old deadline while stopped.
                leaf.call('G', (30000).to_bytes(4, 'big'))
                restarted = status(leaf)
                if not first['running'] or not restarted['running'] or restarted['fault']:
                    raise RuntimeError('Fresh awake window was not established')
                result['restart_window'] = {'before': first, 'after': restarted}
                leaf.call('X')
            for board in boards:
                board.call('X')
                board.call('F', b'\0')
                board.call('G', (570000).to_bytes(4, 'big'))
            deadline = time.monotonic() + 540
            while time.monotonic() < deadline:
                states = [status(b) for b in boards]
                result['snapshots'].append(states)
                if all(s['joined'] and s['authenticated'] for s in states):
                    break
                time.sleep(2)
            else:
                raise TimeoutError('Battery node did not join')
            before = states[1]['application_records']
            if args.usb_awake:
                result['scope'] = 'physical USB-host awake window and fresh application delivery'
                time.sleep(125)  # Exceed the configured 120-second window.
                result['usb_awake'] = status(boards[1])
                if not result['usb_awake']['running'] or result['usb_awake']['fault']:
                    raise RuntimeError('USB-host configuration access was interrupted')
                response = None
            else:
                try:
                    response = boards[1].call('E', (5000).to_bytes(4, 'big'), timeout=30)
                except TimeoutError as error:
                    response = None
                    result['usb_sleep_reply_error'] = str(error)
                    deadline = min(deadline, time.monotonic() + 90)
            if response is not None and (len(response) != 4 or not 4500 <= int.from_bytes(response, 'big') <= 15000):
                raise RuntimeError('Timer sleep was not observed')
            result['elapsed_sleep_ms'] = int.from_bytes(response, 'big') if response is not None else None
            message = boards[0].call('S', args.leaf.to_bytes(2, 'big') + args.sequence.to_bytes(4, 'big'))
            result['message_id'] = message.hex()
            while time.monotonic() < deadline:
                outcome = boards[0].call('Q', message)
                if outcome == b'\x01\x05':
                    result['outcome'] = 'SATISFIED/APPLICATION_ACCEPTED'
                    if response is None and not args.usb_awake:
                        raise RuntimeError('Radio delivery recovered; USB sleep timing and receiver ledger remain unobserved')
                    after = status(boards[1])['application_records']
                    if after != before + 1:
                        raise RuntimeError('Application ledger does not reconcile')
                    result.update(result='PASS', initial_records=before, final_records=after,
                                  outcome='SATISFIED/APPLICATION_ACCEPTED')
                    break
                time.sleep(2)
            else:
                raise TimeoutError('Post-wake application receipt not observed; message retained')
        except Exception as error:
            result['error'] = str(error)
            raise
        finally:
            result['cleanup'] = []
            for board in boards:
                try:
                    board.call('X', timeout=5)
                    result['cleanup'].append({'node': board.node, 'stopped': True})
                except Exception as error:
                    result['cleanup'].append({'node': board.node, 'error': str(error)})
                board.close()
            result['finished'] = time.time()
            (args.output / 'result.json').write_text(json.dumps(result, indent=2)+'\n', encoding='utf8')
            print(json.dumps({k: v for k, v in result.items() if k != 'snapshots'}), flush=True)


if __name__ == '__main__':
    main()
