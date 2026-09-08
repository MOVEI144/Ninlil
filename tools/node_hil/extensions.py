"""Bounded actual-device collection, bulk restart and measured power observation."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import time
from console import Board
from campaign import status, peer_status, flow_status
from hardware import require


def inspect(board):
    data = board.call('J')
    require(len(data) == 65, 'Malformed bulk status')
    return {'id': data[:16].hex(), 'length': int.from_bytes(data[16:20], 'big'),
            'sha256': data[20:52].hex(), 'stored': int.from_bytes(data[52:56], 'big'),
            'acknowledged': int.from_bytes(data[56:58], 'big'),
            'sending': data[58], 'ready': data[59], 'remote_stored': data[60],
            'last_result': int.from_bytes(data[61:65], 'big', signed=True)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output', type=Path)
    parser.add_argument('--seconds', type=int, default=570)
    parser.add_argument('--bytes', type=int, default=512)
    parser.add_argument('--restart', action='store_true')
    parser.add_argument('--drain-relay', action='store_true', help='Keep node 2 online while safely returning custody and withdrawing relay service')
    args = parser.parse_args()
    require(60 <= args.seconds <= 570 and 80 <= args.bytes <= 4096, 'Unbounded fixture request')
    args.output.mkdir(parents=True, exist_ok=False)
    body = bytes((i * 37 + i // 256) % 256 for i in range(args.bytes))
    digest = hashlib.sha256(body).digest()
    identity = hashlib.sha256(b'Ninlil extensions object v1' + digest).digest()[:16]
    result = {'scope': 'actual three-board extension bench', 'started': time.time(),
              'runner_sha256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
              'options': {'restart': args.restart, 'drain_relay': args.drain_relay, 'seconds': args.seconds},
              'bytes': len(body), 'sha256': digest.hex(), 'id': identity.hex(),
              'snapshots': [], 'restart': None, 'pass': False}
    boards = {}
    with (args.output / 'console.jsonl').open('x', encoding='utf-8') as log:
        try:
            for i in (1, 2, 3):
                boards[i] = Board(i, log)
                boards[i].call('X')
                boards[i].call('F', b'\0')
                boards[i].call('G', (600000).to_bytes(4, 'big'), timeout=30)
                boards[i].call('K', timeout=30)
            boards[1].call('O', b'\0\3\1')
            if args.drain_relay:
                boards[2].call('D', b'\x01')
            boards[3].call('O', b'\0\1\0')
            boards[1].call('M', identity + len(body).to_bytes(4, 'big') + digest)
            for offset in range(0, len(body), 40):
                boards[1].call('W', offset.to_bytes(4, 'big') + body[offset:offset+40])
            boards[1].call('V', timeout=30)
            deadline = time.monotonic() + args.seconds
            while time.monotonic() < deadline:
                source, receiver = inspect(boards[1]), inspect(boards[3])
                snapshot = {'time': time.time(), 'source': source, 'receiver': receiver,
                            'nodes': [status(boards[i]) for i in (1, 2, 3)],
                            'power': [list(struct.unpack('bbB', boards[i].call('T'))) for i in (1, 2, 3)]}
                result['snapshots'].append(snapshot)
                if len(result['snapshots']) % 5 == 0:
                    snapshot['peers'] = {str(i): {str(j): peer_status(boards[i], j)
                        for j in (1, 2, 3) if i != j} for i in (1, 2, 3)}
                    snapshot['flow'] = flow_status(boards[1], 1, 3)
                require(all(n['running'] and not n['fault'] for n in snapshot['nodes']), 'Owner fault')
                require(all(-9 <= p[0] <= -3 and -9 <= p[1] <= -3 and p[2] == 1 for p in snapshot['power']), 'Power out of provisioned policy')
                if args.restart and result['restart'] is None and 80 <= receiver['stored'] < len(body):
                    boards[3].call('K', timeout=30)
                    boards[3].call('R')
                    boards[3].close()
                    time.sleep(3)
                    boards[3] = Board(3, log)
                    boards[3].call('G', (600000).to_bytes(4, 'big'), timeout=30)
                    boards[3].call('O', b'\0\1\0')
                    resumed = inspect(boards[3])
                    require(resumed['id'] == identity.hex() and resumed['stored'] >= receiver['stored'], 'Bulk restart lost committed bytes')
                    result['restart'] = {'before': receiver, 'after': resumed}
                if source['remote_stored']:
                    require(receiver['ready'] and receiver['stored'] == len(body), 'Fragment receipt mistaken for complete object')
                    actual = b''.join(boards[3].call('Y', struct.pack('>IH', offset, min(120, len(body)-offset)))
                                      for offset in range(0, len(body), 120))
                    require(actual == body and hashlib.sha256(actual).digest() == digest, 'Readback differs')
                    boards[1].call('C', timeout=30)
                    boards[3].call('C', timeout=30)
                    require(inspect(boards[1])['remote_stored'] and inspect(boards[3])['ready'], 'Collection changed completion')
                    require(not args.restart or result['restart'] is not None, 'Requested restart boundary unobserved')
                    result['pass'] = True
                    break
                time.sleep(3)
            require(result['pass'], 'Bulk campaign timed out; partial progress retained')
        except BaseException as error:
            result['error'] = repr(error)
            raise
        finally:
            result['cleanup'] = []
            for i, board in boards.items():
                try:
                    if args.drain_relay and i == 2:
                        board.call('D', b'\x00', expected=None)
                    board.call('X')
                    result['cleanup'].append({'node': i, 'status': status(board)})
                except Exception as error:
                    result['cleanup'].append({'node': i, 'error': repr(error)})
                board.close()
            result['finished'] = time.time()
            (args.output / 'result.json').write_text(json.dumps(result, indent=2)+'\n', encoding='utf-8')
    print(json.dumps({'pass': result['pass'], 'bytes': len(body), 'seconds': result['finished']-result['started']}))


if __name__ == '__main__':
    main()
