"""Independently reconcile completed public console captures with campaign claims."""
import argparse
import hashlib
import json
from pathlib import Path
from hardware import require


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def reconcile(capture):
    result = json.loads((capture / 'result.json').read_text(encoding='utf-8'))
    raw = capture / 'console.jsonl'
    rows = [json.loads(line) for line in raw.read_text(encoding='utf-8').splitlines()]
    rows = [r for r in rows if r['request'] is not None]
    # Host wall-clock readings can tie; the serialized capture order is exact.
    for order, row in enumerate(rows):
        row['order'] = order
    require(result['result'] == 'PASS' and not result.get('stop_errors'), 'Incomplete campaign')
    require(all(row['result'] == 0 for row in rows), 'Console command failure in accepted capture')
    message = result['message_id']
    submitted = next(row for row in rows if row['command'] == 'S' and row['response'] == message)
    accepted = next(row for row in rows if row['node'] == 1 and row['command'] == 'Q' and
                    row['request'] == message and row['response'] == '0105')
    require(accepted['order'] > submitted['order'], 'Receipt predates submission')
    statuses = [row for row in rows if row['node'] == 3 and row['command'] == 'H' and
                bytes.fromhex(row['response'])[0] == 1]
    counts = [int.from_bytes(bytes.fromhex(row['response'])[10:12], 'big') for row in statuses]
    growth = result.get('application_record_growth', 1)
    require(growth in (0, 1), 'Unsupported application growth')
    if growth == 0:
        require(result.get('initial_source_evidence') == '0000' and counts[0] > 0 and
                any(row['node'] == 1 and row['command'] == 'Q' and row['request'] == message and
                    row['response'] == '0000' and row['order'] < submitted['order'] for row in rows),
                'Recovery did not start with a pending source and an existing receiver record')
    require(counts[0] == result['initial_application_records'] and counts[-1] == counts[0]+growth,
            'Application count did not reconcile')
    require(all(counts[0] <= count <= counts[-1] for count in counts), 'Application ledger went backwards')
    for node in (1, 2, 3):
        last = [row for row in rows if row['node'] == node][-1]
        require(last['command'] == 'X', 'Capture does not end with each owner stopped')
    resets = [row for row in rows if row['command'] == 'R']
    if 'before_reset' in result:
        require([row['node'] for row in resets] == result.get('reset_nodes', [2, 3]) and
                all(submitted['order'] < row['order'] < accepted['order'] for row in resets),
                'Required MCU resets did not occur during this delivery')
        first, last = resets[0]['order'], resets[-1]['order']
        for node, offset in ((2, 8), (3, 10)):
            samples = [row for row in rows if row['node'] == node and row['command'] == 'H' and
                       bytes.fromhex(row['response'])[0] == 1]
            before = [row for row in samples if row['order'] < first][-1]
            after = next(row for row in samples if row['order'] > last)
            values = [int.from_bytes(bytes.fromhex(r['response'])[offset:offset+2], 'big')
                      for r in (before, after)]
            require(values[0] > 0 and values[1] >= values[0] if node == 2 else
                    values[0] == values[1] == counts[0], 'Custody/held application capture did not reconcile')
        for reset in resets:
            boot = next(row for row in rows if row['order'] > reset['order'] and
                        row['node'] == reset['node'] and row['command'] == 'H')
            require(bytes.fromhex(boot['response'])[0] == 0, 'Reset did not boot stopped')
    if result.get('relay_removal_ready'):
        require(any(row['node'] == 2 and row['command'] == 'D' and row['request'] == '' and
                    row['response'] == '01' and row['order'] < submitted['order'] for row in rows),
                'Removal readiness was not observed before fresh submission')
        require(result.get('relay_resumed') and any(row['node'] == 2 and row['command'] == 'D' and
                row['request'] == '00' and row['order'] > accepted['order'] for row in rows), 'Relay not resumed')
    return {'campaign': capture.name, 'message_id': message, 'result': 'PASS',
            'duration_seconds': round(result['finished']-result['started'], 3),
            'application_records': [counts[0], counts[-1]], 'application_record_growth': growth,
            'mcu_resets': [r['node'] for r in resets],
            'relay_removal_ready': bool(result.get('relay_removal_ready')),
            'source_evidence': accepted['response'], 'console_sha256': digest(raw),
            'result_sha256': digest(capture / 'result.json'), 'console_records': len(rows)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('captures', nargs='+', type=Path)
    args = parser.parse_args()
    for capture in args.captures:
        print(json.dumps(reconcile(capture), sort_keys=True))


if __name__ == '__main__':
    main()
