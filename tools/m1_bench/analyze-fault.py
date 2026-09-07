import collections
import hashlib
import json
import re
import subprocess
import sys
from pathlib import Path

root = Path(__file__).resolve().parents[2] / ".verify-m1-evidence"
label, mode = sys.argv[1:3]
assert mode in ('new', 'replay') and re.fullmatch(r'[a-z0-9-]+', label)
meta = json.loads((root / f'{label}-capture.json').read_text())
assert not meta['failed']
campaign = meta['campaign']
sender, receiver = meta['sender'], meta['receiver']
nodes = {'a': 1, 'b': 2}
result = {'label': label, 'mode': mode, 'campaign': campaign, 'source_commit': meta['source_commit'], 'boards': {}}
logs = {}
records = {}
for board in (sender, receiver):
    state = meta['boards'][board]
    assert state['ready'] and 'error' not in state and 'stop_error' not in state
    raw = (root / f'{label}-{board}.log').read_bytes()
    log = raw.decode('ascii')
    logs[board] = log
    assert len(raw) == state['bytes'] and hashlib.sha256(raw).hexdigest() == state['log_sha256']
    assert raw.endswith(b'\n') and log.count('ESP-ROM:') == (2 if board == receiver and mode == 'new' else 1)
    assert 'rst:0x15 (USB_UART_CHIP_RESET)' in log
    assert not re.search(r'(^E \(|panic|Guru Meditation|result=FAIL)', log, re.M)
    if mode == 'replay':
        assert 'HIL_LINK_SENT' not in log, 'Completed work must not retransmit on replay'
    assert f'NINLIL_HIL_DELIVERY_READY campaign={campaign} node={nodes[board]} peer={3-nodes[board]}' in log
    assert "profile region='JP' freq=921400000 tx=enabled power=-9 SF=7 BW=125000" in log
    folder = root / (state['variant'] + '-r1')
    for line in (folder / 'SHA256SUMS').read_text().splitlines():
        digest, name = line.split()
        assert Path(name).name == name and hashlib.sha256((folder / name).read_bytes()).hexdigest() == digest
    assert (folder / 'source-commit.txt').read_text().strip() == meta['source_commit']
    image_info = subprocess.check_output([sys.executable, '-m', 'esptool', 'image-info', str(folder / 'ninlil_m1.bin')], text=True)
    (root / f"{label}-{state['variant']}-image-info.log").write_text(image_info)
    elf_hash = re.search(r'ELF file SHA256: ([0-9a-f]{64})', image_info)[1]
    prefix = re.search(r'ELF file SHA256: +([0-9a-f]+)', log)[1]
    assert len(prefix) == 9 and elf_hash.startswith(prefix)
    assert 'App version: 3254ae3' in image_info and 'App version:      3254ae3' in log
    journal = root / f'{label}-{board}-journal.bin'
    journal_meta = json.loads(journal.with_suffix('.json').read_text())
    digest = hashlib.sha256(journal.read_bytes()).hexdigest()
    assert journal_meta['sha256'] == digest and journal_meta['md5_device_verified']
    assert journal_meta['app_device_md5_verified']
    assert journal_meta['app_sha256'] == hashlib.sha256((folder / 'ninlil_m1.bin').read_bytes()).hexdigest()
    assert journal.stat().st_size == journal_meta['bytes'] == 131072
    inspected = subprocess.check_output(['docker', 'exec', 'ninlil-static-star-verify',
        '/tmp/ninlil-fault-final/gcc/m1_flash_inspect', '/work/.verify-m1-evidence/' + journal.name], text=True)
    (root / f'{label}-{board}-journal-inspect.log').write_text(inspected)
    marker = re.search(r'VALIDATED records=(\d+) next_sequence=(\d+) append_offset=(\d+)\n$', inspected)
    assert marker
    board_records = [(int(t), int(offset), bytes.fromhex(body)) for t, offset, body in
        re.findall(r'RECORD type=(\d+) offset=(\d+) data=([0-9a-f]+)', inspected)]
    assert len(board_records) == int(marker[1]) and int(marker[2]) == len(board_records) + 1
    assert all(body[0] == 5 for _, _, body in board_records)
    records[board] = board_records
    result['boards'][board] = {'serial': state['serial'], 'variant': state['variant'],
        'begin_utc': state['begin_utc'], 'end_utc': state['end_utc'],
        'log_sha256': state['log_sha256'], 'log_bytes': len(raw),
        'app_sha256': hashlib.sha256((folder / 'ninlil_m1.bin').read_bytes()).hexdigest(),
        'elf_sha256': elf_hash, 'journal_sha256': digest,
        'journal_record_counts': dict(collections.Counter(t for t, _, _ in board_records)),
        'journal_append_offset': int(marker[3]), 'radio_tx_done_logs': log.count('HIL_LINK_SENT bytes=')}

submits = re.findall(r'HIL_SUBMIT campaign=(\d+) seq=(\d+) id=([0-9a-f]{32}) outcome=(\d+) evidence=(\d+)', logs[sender])
satisfied = re.findall(r'HIL_SATISFIED campaign=(\d+) seq=(\d+) id=([0-9a-f]{32}) evidence=(\d+)', logs[sender])
assert len(submits) == len(satisfied) == 100
ids = {}
for c, seq, identifier, outcome, evidence in submits:
    seq = int(seq)
    assert int(c) == campaign and 1 <= seq <= 100 and seq not in ids
    assert int(outcome) == (0 if mode == 'new' else 1)
    assert int(evidence) == 0 if mode == 'new' else int(evidence) in (4, 5)
    ids[seq] = identifier
assert len(set(ids.values())) == 100
assert [(int(c), int(seq), identifier) for c, seq, identifier, _ in satisfied] == [(campaign, seq, ids[seq]) for seq in range(1, 101)]
assert all(int(evidence) in (4, 5) for _, _, _, evidence in satisfied)
assert f'NINLIL_HIL_DELIVERY result=PASS node={nodes[sender]} peer={nodes[receiver]} count=100 campaign={campaign}' in logs[sender]
stack = re.search(r'stack phase=delivery-complete minimum-free=(\d+)/(\d+) bytes', logs[sender])
assert stack and int(stack[1]) >= int(stack[2]) // 4

for marker_name in ('HIL_STORED', 'HIL_CONSUMED'):
    received = re.findall(marker_name + r' campaign=(\d+) seq=(\d+) id=([0-9a-f]{32})', logs[receiver])
    assert len(received) == (100 if mode == 'new' else 0)
    if mode == 'new':
        assert [(int(c), int(s), identifier) for c, s, identifier in received] == [(campaign, s, ids[s]) for s in range(1, 101)]

for board in (sender, receiver):
    create_type = 1 if board == sender else 5
    expected_types = {1, 2, 3} if board == sender else {5, 6, 7}
    assert all(t in expected_types for t, _, _ in records[board])
    creates = [body for t, _, body in records[board] if t == create_type]
    assert len(creates) == 100
    for seq, body in enumerate(creates, 1):
        header_size = 52 if board == sender else 36
        assert len(body) == header_size + 12
        assert body[:6] == bytes([5, 1, 4, 2, 0, 0])
        assert int.from_bytes(body[6:8], 'big') == nodes[receiver if board == sender else sender]
        assert body[8:12] == bytes.fromhex('0100000c') and body[12:20] == bytes(8)
        assert body[20:36].hex() == ids[seq]
        payload = campaign.to_bytes(4, 'big') + nodes[sender].to_bytes(2, 'big') + nodes[receiver].to_bytes(2, 'big') + seq.to_bytes(4, 'big')
        assert body[header_size:] == payload
        if board == sender:
            assert body[36:52] == b'HIL2' + payload
    for record_type in expected_types - {create_type}:
        matching = [body for t, _, body in records[board] if t == record_type]
        assert {body[1:17].hex() for body in matching} == set(ids.values())
        assert all(len(body) == {2:17, 3:20, 6:19, 7:17}[record_type] for body in matching)
        if record_type == 3:
            assert all(body[17] in (4, 5) and int.from_bytes(body[18:20], 'big') < 128 for body in matching)
    # Every associated transition must follow its committed create record.
    created = set()
    for t, _, body in records[board]:
        if t == create_type:
            created.add(body[20:36].hex())
        else:
            assert body[1:17].hex() in created

if mode == 'new':
    cases = {
        sender: {'drop-receipt-after-rx': 1, 'duplicate-receipt-after-rx': 1,
                 'masked-dio1-tx': 5},
        receiver: {'drop-data-after-rx': 1, 'duplicate-data-after-rx': 2,
                   'reserved-flags-after-rx': 3, 'wrong-target-after-rx': 4,
                   'hold-receipt-until-restart': 6,
                   'receiver-restart-before-receipt': 6}}
    observed = []
    for board, expected in cases.items():
        events = re.findall(r'HIL_FAULT kind=([a-z0-9-]+) seq=(\d+) id=([0-9a-f]{32})', logs[board])
        assert len(events) == len(expected), events
        assert {kind for kind, _, _ in events} == set(expected)
        for kind, seq, identifier in events:
            assert identifier == ids[expected[kind]]
            assert int(seq) == (0 if 'receipt-after-rx' in kind else expected[kind])
            observed.append({'board': board, 'kind': kind, 'sequence': expected[kind], 'id': identifier})
    tx = re.findall(r'HIL_FAULT_TX_RESULT rc=(-?\d+) expected=(-?\d+)', logs[sender])
    assert len(tx) == 1 and tx[0][0] == tx[0][1] and int(tx[0][0]) < 0
    rlog = logs[receiver]
    hold = rlog.index('HIL_FAULT kind=hold-receipt-until-restart')
    reset = rlog.index('HIL_FAULT kind=receiver-restart-before-receipt')
    assert hold < rlog.index(f'HIL_CONSUMED campaign={campaign} seq=6 ') < reset
    assert 'HIL_LINK_SENT' not in rlog[hold:reset]
    assert rlog.count('NINLIL_HIL_DELIVERY_READY') == 2
    result['fault_events'] = observed
    result['tx_timeout_code'] = int(tx[0][0])
    result['receiver_reset_reasons'] = re.findall(r'rst:[^\r\n]+', rlog)

result.update(result='PASS', distinct_messages=100, missing=0, duplicate_ownership_records=0,
    sender_stack_minimum_free_bytes=int(stack[1]), message_ids=ids,
    evidence='REMOTE_STORED and SATISFIED; consumer retirement is harness-only, not product application acceptance')
with (root / f'{label}-analysis.json').open('x') as output:
    json.dump(result, output, indent=2)
print(json.dumps(result, indent=2))
