"""Actual MCU reset: saved membership must not restore a crypto session."""
import json
import sys
from datetime import datetime, timezone

import campaign as c
from hardware import EVIDENCE

label, previous = sys.argv[1:]
assert c.re.fullmatch(r'secure-20260908-[a-z0-9-]+', label)
prior = json.loads((EVIDENCE / f'{previous}-result.json').read_text())
assert prior['result'] == 'PASS'
boards = []
result = {'label': label, 'started': datetime.now(timezone.utc).isoformat()}
try:
    old_keys = []
    stale = None
    for name in ('a', 'b'):
        entries = [json.loads(line) for line in (EVIDENCE / f'{previous}-{name}.jsonl').read_text().splitlines()]
        old_keys.append(next(bytes.fromhex(e['rx'].split()[3]) for e in entries if e.get('rx', '').startswith('BENCH I 0 ')))
        if name == 'a':
            stale = next(bytes.fromhex(e['rx'].split()[3]) for e in entries if e.get('rx', '').startswith('BENCH S 0 '))
        boards.append(c.Board(name, label))
    a, b = boards
    keys = [board.call('I') for board in boards]
    assert all(new != old for new, old in zip(keys, old_keys))
    assert a.call('Z')[:4] == b'NJ\x01\x03'
    assert b.call('Z')[:4] == b'NJ\x01\x02'
    for board in boards:
        board.call('S', b'no restored keys', expected=-1)
        board.call('O', expected=-14)
    a.call('L', expected=-12)
    c.passed('actual MCU reset retained committed Join/revoke records but no session or permission')
    fingerprint = c.handshake(a, b, keys)
    assert fingerprint.hex() != prior['tests'][2]['fingerprint']
    rc, _ = b.call('U', c.air(a, b, stale), expected=None)
    assert rc != 0
    a.call('L', expected=-12)  # EDHOC alone is not re-enrollment.
    for sender, receiver in [(a, b), (b, a)]:
        for i in range(20):
            c.encrypted(sender, receiver, i.to_bytes(4, 'big') + bytes([i]) * 60)
    c.passed('fresh post-reset mutual authentication and encrypted RF; stale ciphertext rejected', each_direction=20)
    result['result'] = 'PASS'
except BaseException as error:
    result.update(result='FAIL', error=repr(error))
    raise
finally:
    for board in boards:
        board.close()
    result.update(tests=c.results, rf_frames=c.rf_frames, finished=datetime.now(timezone.utc).isoformat())
    with (EVIDENCE / f'{label}-result.json').open('x') as output:
        json.dump(result, output, indent=2)
