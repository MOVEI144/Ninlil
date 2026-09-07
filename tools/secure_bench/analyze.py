"""Independent reconciliation of each board's captured command/reply records."""
import hashlib
import json
import re
import sys
from pathlib import Path

from hardware import EVIDENCE


def inspect(label):
    result = json.loads((EVIDENCE / f'{label}-result.json').read_text())
    assert result['result'] == 'PASS'
    records, hashes = {}, {}
    for board in ('a', 'b'):
        path = EVIDENCE / f'{label}-{board}.jsonl'
        hashes[board] = hashlib.sha256(path.read_bytes()).hexdigest()
        pending = None
        exchanges = []
        for raw in path.read_text().splitlines():
            entry = json.loads(raw)
            if 'tx' in entry:
                assert pending is None
                command, _, data = entry['tx'].partition(' ')
                pending = (command, bytes.fromhex(data))
            if entry.get('rx', '').startswith('BENCH '):
                found = re.fullmatch(r'BENCH (.) (-?\d+)(?: ([0-9a-f]*))?', entry['rx'])
                assert found
                command, rc, output = found[1], int(found[2]), bytes.fromhex(found[3] or '')
                if command == '!':
                    assert pending is None and rc == 0
                    continue
                assert pending is not None and pending[0] == command
                exchanges.append((command, pending[1], rc, output))
                pending = None
        assert pending is None
        records[board] = exchanges
    total = 0
    envelopes = 0
    for sender, receiver in [('a', 'b'), ('b', 'a')]:
        sent = [data for command, data, rc, _ in records[sender] if command == 'T' and rc == 0]
        received = [out for command, _, rc, out in records[receiver] if command == 'R' and rc == 0]
        assert sent == received and sent
        assert all(1 <= len(frame) <= 240 for frame in sent)
        total += len(sent)
        last = {}
        for command, data, rc, output in records[sender]:
            if command in ('S', 'C') and rc == 0:
                assert output[:3] == b'NS\x01' and len(output) == len(data) + 40
                assert output[31] == (command == 'C')
                key = output[3:4] + output[8:24]
                counter = int.from_bytes(output[24:29], 'big')
                assert key not in last or counter > last[key]
                last[key] = counter
                envelopes += 1
            if command in ('U', 'D') and rc == 0:
                assert data in [out for cmd, _, status, out in records[sender] if cmd == 'R' and status == 0]
        for command, _, rc, output in records[sender]:
            if rc != 0:
                assert not output
    assert total == result['rf_frames']
    report = {'label': label, 'result': 'PASS', 'paired_RF_frames': total,
              'fresh_monotonic_envelopes': envelopes, 'log_sha256': hashes}
    with (EVIDENCE / f'{label}-analysis.json').open('x') as output:
        json.dump(report, output, indent=2)
    print(json.dumps(report), flush=True)


if __name__ == '__main__':
    for label in sys.argv[1:]:
        assert re.fullmatch(r'secure-20260908-[a-z0-9-]+', label)
        inspect(label)
