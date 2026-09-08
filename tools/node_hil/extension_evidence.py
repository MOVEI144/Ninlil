"""Read-only reconciliation of public USB extension captures; no device calls."""
import argparse
import hashlib
import json
from pathlib import Path


def analyze(path):
    raw = path.read_bytes()
    rows = [json.loads(line) for line in raw.decode().splitlines()]
    latest, stopped, powers, collection, restarts = {}, {}, set(), [], []
    manifest = None
    readback = {}
    source_bytes = {}
    pending_reset = None
    for index, row in enumerate(rows):
        request, response = bytes.fromhex(row['request']), bytes.fromhex(row['response'])
        if len(request) > 128 or len(response) > 128:
            raise ValueError('Unbounded console record')
        node, command = row['node'], row['command']
        if row['result'] != 0:
            continue
        if command == 'M':
            if len(request) != 52:
                raise ValueError('Malformed manifest')
            current = {'id': request[:16].hex(), 'length': int.from_bytes(request[16:20], 'big'),
                       'sha256': request[20:].hex()}
            if manifest and current != manifest:
                raise ValueError('Object identity changed')
            manifest = current
        elif command in ('W', 'Y'):
            if (command == 'W' and not 4 < len(request) <= 44) or (
                command == 'Y' and (len(request) != 6 or len(response) != int.from_bytes(request[4:], 'big'))):
                raise ValueError('Malformed bounded object operation')
            offset = int.from_bytes(request[:4], 'big')
            data = request[4:] if command == 'W' else response
            target = source_bytes if command == 'W' else readback
            for i, value in enumerate(data, offset):
                if i in target and target[i] != value:
                    raise ValueError('Conflicting object bytes')
                target[i] = value
        elif command == 'J':
            if len(response) != 65 or any(value > 1 for value in response[58:61]):
                raise ValueError('Malformed object status')
            current = {'id': response[:16].hex(), 'length': int.from_bytes(response[16:20], 'big'),
                       'sha256': response[20:52].hex(), 'stored': int.from_bytes(response[52:56], 'big'),
                       'acknowledged': int.from_bytes(response[56:58], 'big'),
                       'sending': response[58], 'ready': response[59], 'remote_stored': response[60],
                       'last_result': int.from_bytes(response[61:], 'big', signed=True)}
            latest[node] = current
            if node == 3 and pending_reset:
                before = pending_reset
                restarts.append({'before': before, 'after': current,
                                 'retained': before['id'] == current['id'] and current['stored'] >= before['stored']})
                pending_reset = None
        elif command == 'R' and node == 3:
            pending_reset = latest.get(3)
        elif command == 'H':
            stopped[node] = bool(response and response[0] == 0)
        elif command == 'T':
            if len(response) != 3 or response[2] != 1:
                raise ValueError('Malformed power observation')
            levels = tuple(int.from_bytes(response[i:i+1], 'big', signed=True) for i in (0, 1))
            if any(p not in (-9, -6, -3) for p in levels):
                raise ValueError('Power outside the fixture policy')
            powers.add(levels)
        elif command in ('K', 'C'):
            collection.append({'record': index, 'node': node, 'kind': command})
    def matches(data):
        return bool(manifest and 0 < manifest['length'] <= 65536 and
                    set(data) == set(range(manifest['length'])) and
                    hashlib.sha256(bytes(data[i] for i in range(manifest['length']))).hexdigest() == manifest['sha256'])
    source, receiver = latest.get(1, {}), latest.get(3, {})
    identity_matches = bool(manifest and all(
        all(state.get(key) == value for key, value in manifest.items()) for state in (source, receiver)))
    complete = bool(source.get('remote_stored') and receiver.get('ready') and
                    identity_matches and receiver.get('stored') == source.get('length') and matches(source_bytes) and matches(readback))
    return {'console_sha256': hashlib.sha256(raw).hexdigest(), 'console_bytes': len(raw),
            'records': len(rows), 'elapsed_seconds': rows[-1]['time'] - rows[0]['time'],
            'manifest': manifest, 'source': source, 'receiver': receiver, 'source_bytes_match': matches(source_bytes),
            'readback_matches': matches(readback), 'complete': complete, 'restarts': restarts,
            'collection': collection, 'power_pairs': sorted(powers),
            'power_decrease_observed': any(p[1] < -3 for p in powers),
            'all_stopped': all(stopped.get(i) for i in (1, 2, 3))}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('console', type=Path)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    result = analyze(args.console)
    text = json.dumps(result, indent=2)+'\n'
    if args.output:
        with args.output.open('x', encoding='utf-8') as output:
            output.write(text)
    print(text)


if __name__ == '__main__':
    main()
