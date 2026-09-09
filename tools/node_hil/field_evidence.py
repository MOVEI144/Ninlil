"""Read-only stage/timing reconciliation for field deployment bench captures."""
import argparse
import hashlib
import json
import struct
from pathlib import Path


def peer_status(board, peer):
    b = board.call('B', peer.to_bytes(2, 'big'))
    if len(b) not in (41, 93):
        raise ValueError('Malformed peer status')
    return {'active': b[0], 'revoked': b[1], 'ready': b[2], 'authority_state': b[3],
            'authority_phase': b[4], 'e2e': b[9:25].hex(),
            'hop': b[25:41].hex(),
            'probe_attempts': int.from_bytes(b[5:7], 'big'),
            'probe_delivered': int.from_bytes(b[7:9], 'big')}


def flow_status(board, source, target):
    b = board.call('L', struct.pack('>HH', source, target))
    if len(b) != 66 or b[44] > 5 or b[55] > 5:
        raise ValueError('Malformed flow status')
    result = dict(zip(('lease_ms', 'local_epoch', 'local_until', 'authority_epoch',
                       'authority_until', 'local_ready', 'authority_phase',
                       'reconciled', 'notified'), struct.unpack('>5Q4B', b[:44])))
    result['local_path'] = list(struct.unpack('>5H', b[45:55])[:b[44]])
    result['authority_path'] = list(struct.unpack('>5H', b[56:66])[:b[55]])
    return result


def analyze(folder):
    raw = (folder / 'console.jsonl').read_bytes()
    rows = [json.loads(line) for line in raw.decode('utf-8').splitlines()]
    rows = [r for r in rows if r['request'] is not None]
    result_path = folder / 'result.json'
    result = json.loads(result_path.read_text()) if result_path.exists() else {}
    start = next((r['time'] for r in rows if r['command'] == 'G' and r['result'] == 0), rows[0]['time'])
    latest, identities, reset, stops, counts, masks = {}, {}, [], set(), [], {}
    joined_at = clocks_at = submitted_at = accepted_at = stored_at = None
    message = None
    accepted_messages = []
    for r in rows:
        request, reply = bytes.fromhex(r['request']), bytes.fromhex(r['response'])
        if len(request) > 128 or len(reply) > 128 or r['node'] not in (1, 2, 3):
            raise ValueError('Unbounded or unknown console record')
        if r['result']:
            continue
        node, command, stamp = r['node'], r['command'], r['time']
        if command == 'I':
            if len(reply) != 97 or reply[32] != 4:
                raise ValueError('Malformed public identity')
            identities.setdefault(node, set()).add(hashlib.sha256(reply).hexdigest())
        elif command == 'R':
            reset.append(node)
        elif command == 'G':
            stops.discard(node)
        elif command == 'X':
            stops.add(node)
        elif command == 'F':
            masks[node] = int.from_bytes(request, 'big')
        elif command == 'H':
            if len(reply) != 96 or any(x > 1 for x in reply[:3]):
                raise ValueError('Malformed runtime status')
            if int.from_bytes(reply[36:40], 'big'):
                raise ValueError('Runtime fault in capture')
            latest[node] = (stamp, reply[:3])
            current = len(latest) == 3 and all(stamp - t <= 6 for t, _ in latest.values())
            if current and all(b[0] and b[1] for _, b in latest.values()):
                joined_at = stamp if joined_at is None else joined_at
                if all(b[2] for _, b in latest.values()):
                    clocks_at = stamp if clocks_at is None else clocks_at
            if node == 3 and reply[0]:
                count = int.from_bytes(reply[10:12], 'big')
                if counts and count > counts[0] and stored_at is None:
                    stored_at = stamp
                counts.append(count)
        elif command == 'S':
            if len(reply) != 16 or (message and reply != message and accepted_at is None):
                raise ValueError('Conflicting source message')
            if message != reply:
                accepted_at = None
            message, submitted_at = reply, stamp
        elif command == 'Q' and message and request == message and reply == b'\x01\x05':
            if accepted_at is None:
                accepted_messages.append({'id': message.hex(), 'submitted_seconds': round(submitted_at - start, 3),
                                          'accepted_seconds': round(stamp - start, 3)})
            accepted_at = stamp if accepted_at is None else accepted_at
    delivery = 'message_id' in result
    if result.get('result') == 'PASS':
        if stops != {1, 2, 3} or result.get('stop_errors'):
            raise ValueError('Claimed pass lacks stopped owners')
        if delivery and (accepted_at is None or not counts or counts[-1] - counts[0] != result['application_record_growth']):
            raise ValueError('Claimed delivery does not reconcile with device replies')
        if delivery and len(accepted_messages) != result.get('message_count', 1):
            raise ValueError('Claimed message count does not reconcile')
        if not delivery and (reset != [1, 2, 3] or len(identities) != 3 or any(len(v) != 1 for v in identities.values())):
            raise ValueError('Claimed setup/reset identity preservation does not reconcile')
    elapsed = lambda stamp: round(stamp - start, 3) if stamp is not None else None
    return {'capture': folder.name, 'reported_result': result.get('result', 'INTERRUPTED'),
            'console_sha256': hashlib.sha256(raw).hexdigest(), 'console_records': len(rows),
            'result_sha256': hashlib.sha256(result_path.read_bytes()).hexdigest() if result_path.exists() else None,
            'receiver_counts': [counts[0], counts[-1]] if counts else [],
            'all_joined_seconds': elapsed(joined_at), 'all_clocks_seconds': elapsed(clocks_at),
            'submitted_seconds': elapsed(submitted_at), 'receiver_growth_seconds': elapsed(stored_at),
            'source_acceptance_seconds': elapsed(accepted_at),
            'message_id': message.hex() if message else None, 'accepted_messages': accepted_messages, 'fault_masks': masks,
            'software_resets': reset, 'all_stopped': stops == {1, 2, 3},
            'physical_range_verified': False, 'physical_application_verified': False}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('captures', type=Path, nargs='+')
    args = parser.parse_args()
    for path in args.captures:
        print(json.dumps(analyze(path), sort_keys=True))
