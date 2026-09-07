"""Capture the restored, frequency-zero firmware's 100 initialization cycles."""
import hashlib
import json
import sys
import time
from hardware import EVIDENCE, backup, connect

board = sys.argv[1]
assert board in ('a', 'b')
assert f'ORIGINAL_FULL_FLASH_RESTORED {board}' in (EVIDENCE / f'secure-20260908-restore-{board}.log').read_text()
esp = connect(board)
try:
    assert esp.flash_md5sum(0, 0x800000).lower() == hashlib.md5(backup(board)).hexdigest()
    port = esp._port
    port.timeout = 0.2
    port.reset_input_buffer()
    esp.hard_reset()
    start = time.monotonic()
    captured = bytearray()
    with (EVIDENCE / f'secure-20260908-idle-{board}.log').open('xb') as output:
        while time.monotonic() - start < 45:
            data = port.read(min(max(port.in_waiting, 1), 4096))
            output.write(data)
            output.flush()
            captured.extend(data)
            assert len(captured) < 1024*1024
            assert b'Guru Meditation' not in captured
            if b'operational RF disabled: frequency is unset' in captured:
                break
    assert b'NINLIL_HIL_INIT result=PASS cycles=100' in captured
    assert b'freq=0 tx=disabled' in captured
    assert b'operational RF disabled: frequency is unset' in captured
    result = {'board': board, 'result': 'PASS', 'initialization_cycles': 100,
              'frequency_hz': 0, 'tx_disabled': True,
              'log_sha256': hashlib.sha256(captured).hexdigest()}
    with (EVIDENCE / f'secure-20260908-idle-{board}.json').open('x') as output:
        json.dump(result, output, indent=2)
    print(json.dumps(result), flush=True)
finally:
    esp._port.close()
