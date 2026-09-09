import hashlib
import json
import re
import sys
import threading
import time
import traceback
from datetime import datetime, timezone
from pathlib import Path
from esptool.cmds import detect_chip
from esptool.reset import USBJTAGSerialReset
from serial.tools import list_ports

root = Path(__file__).resolve().parents[2] / ".verify-m1-evidence"
sender_variant, receiver_variant, label = sys.argv[1:4]
assert re.fullmatch(r'[a-z0-9-]+', label)
assert sender_variant in ('fault-a-init', 'fault-b-init', 'recovery-a-init')
assert receiver_variant == {'fault-a-init': 'fault-b-resp', 'fault-b-init': 'fault-a-resp', 'recovery-a-init': 'recovery-b-resp'}[sender_variant]
campaign = {'fault-a-init': 2026090704, 'fault-b-init': 2026090705, 'recovery-a-init': 2026090703}[sender_variant]
sender_board = sender_variant.split('-')[1]
receiver_board = receiver_variant.split('-')[1]
identities = {'a': ('COM3', 'E0:72:A1:F7:FF:0C', 1, 2), 'b': ('COM5', 'E0:72:A1:D8:3E:74', 2, 1)}
ready, stop, failed = threading.Event(), threading.Event(), threading.Event()
outcomes = {}

def capture(variant):
    board = variant.split('-')[1]
    port_name, identity, node, peer = identities[board]
    state = {'board': board, 'variant': variant, 'serial': identity, 'port': port_name,
        'begin_utc': datetime.now(timezone.utc).isoformat()}
    esp = None
    try:
        assert f'FLASH_VERIFIED variant={variant}' in (root / f'{variant}-r1-flash.log').read_text()
        found = [p for p in list_ports.comports() if p.device == port_name]
        assert len(found) == 1 and found[0].serial_number == identity
        esp = detect_chip(port_name, baud=115200, connect_mode='no-reset', connect_attempts=3)
        assert bytes(esp.read_mac()).hex() == identity.replace(':', '').lower()
        port = esp._port
        port.timeout = 0.2
        port.set_buffer_size(rx_size=1048576, tx_size=65536)
        with (root / f'{label}-{board}.log').open('xb') as output:
            port.reset_input_buffer()
            esp.hard_reset()
            began = time.monotonic()
            tail = b''
            digest = hashlib.sha256()
            total = 0
            final_at = None
            last_progress = began
            while not stop.is_set() and time.monotonic() - began < 650:
                data = port.read(min(max(port.in_waiting, 1), 4096))
                total += len(data)
                assert total <= 4 * 1024 * 1024
                output.write(data)
                output.flush()
                digest.update(data)
                tail = (tail + data)[-65536:]
                if b'App version:      3254ae3' in tail and f'NINLIL_HIL_DELIVERY_READY campaign={campaign} node={node} peer={peer}'.encode() in tail:
                    state['ready'] = True
                    if board == receiver_board:
                        ready.set()
                if re.search(rb'(^|\n)E \(', tail) or b'Guru Meditation' in tail or b'result=FAIL' in tail:
                    raise RuntimeError('Firmware reported an error; preserve logs and stop')
                if board == sender_board:
                    if b'HIL_SATISFIED ' in data:
                        last_progress = time.monotonic()
                    if time.monotonic() - last_progress > 60:
                        raise RuntimeError('No durable progress for 60 seconds; stop for diagnosis')
                    marker = re.search(rb'NINLIL_HIL_DELIVERY result=PASS[^\r\n]*', tail)
                    stack = re.search(rb'stack phase=delivery-complete minimum-free=(\d+)/(\d+) bytes', tail)
                    if marker and stack:
                        assert int(stack[1]) >= int(stack[2]) // 4
                        if final_at is None:
                            final_at = time.monotonic()
                            state['summary'] = marker[0].decode('ascii')
                        if time.monotonic() - final_at >= 2:
                            stop.set()
            state.update(bytes=total, log_sha256=digest.hexdigest())
            if board == sender_board and 'summary' not in state:
                raise RuntimeError('Capture ended without complete durable marker')
    except Exception as error:
        failed.set()
        stop.set()
        state.update(error=str(error), traceback=traceback.format_exc())
    finally:
        if esp is not None:
            try:
                USBJTAGSerialReset(esp._port)()
                state['end_action'] = 'MCU held in download mode for journal readback'
            except Exception as error:
                state['stop_error'] = str(error)
                failed.set()
            esp._port.close()
        state['end_utc'] = datetime.now(timezone.utc).isoformat()
        outcomes[board] = state

receiver = threading.Thread(target=capture, args=(receiver_variant,))
receiver.start()
for _ in range(200):
    if ready.wait(0.1) or stop.is_set():
        break
if ready.is_set() and not stop.is_set():
    sender = threading.Thread(target=capture, args=(sender_variant,))
    sender.start()
    sender.join(660)
    if sender.is_alive():
        failed.set()
        stop.set()
else:
    failed.set()
    stop.set()
receiver.join(10)
result = {'label': label, 'campaign': campaign, 'sender': sender_board, 'receiver': receiver_board,
    'source_commit': '3254ae3f4f76a51061c2d9a79b1b37eb8804c302',
    'failed': failed.is_set(), 'boards': outcomes}
with (root / f'{label}-capture.json').open('x') as output:
    json.dump(result, output, indent=2)
print(json.dumps(result, indent=2), flush=True)
sys.exit(1 if failed.is_set() else 0)
