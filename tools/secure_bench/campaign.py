"""Two-MCU crypto/Flash tests with every inter-board frame sent over SX1262.

USB invokes typed library boundaries; this is NOT a production pump/Relay test.
Only generated non-secret test plaintext and public handshake data are recorded.
"""
import json
import re
import secrets
import sys
import time
from datetime import datetime, timezone
from pathlib import Path

from esptool.cmds import detect_chip
from esptool.reset import USBJTAGSerialReset
from hardware import EVIDENCE, IDENTITIES


class Board:
    def __init__(self, name, label):
        self.name = name
        self.log = (EVIDENCE / f'{label}-{name}.jsonl').open('x', encoding='utf-8')
        port, identity = IDENTITIES[name]
        esp = detect_chip(port, baud=115200, connect_mode='no-reset', connect_attempts=3)
        assert bytes(esp.read_mac()).hex() == identity.replace(':', '').lower()
        self.port = esp._port
        self.port.timeout = 0.2
        self.port.set_buffer_size(rx_size=1048576, tx_size=65536)
        self.port.reset_input_buffer()
        esp.hard_reset()
        self.response('!', 15)

    def response(self, command, timeout=5):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            raw = self.port.readline()
            if not raw:
                continue
            text = raw.decode('ascii', errors='replace').strip()
            self.log.write(json.dumps({'utc': datetime.now(timezone.utc).isoformat(), 'rx': text}) + '\n')
            self.log.flush()
            assert 'Guru Meditation' not in text and 'panic' not in text, text
            found = re.fullmatch(r'BENCH (.) (-?\d+)(?: ([0-9a-f]*))?', text)
            if found:
                assert found[1] == command, (command, text)
                return int(found[2]), bytes.fromhex(found[3] or '')
        raise TimeoutError(f'{self.name} command {command}')

    def call(self, command, data=b'', expected=0):
        wire = f'{command} {data.hex()}\n'.encode()
        assert len(wire) <= 2051
        self.log.write(json.dumps({'tx': wire.decode().strip()}) + '\n')
        self.log.flush()
        self.port.write(wire)
        rc, output = self.response(command)
        if expected is not None:
            assert rc == expected, (self.name, command, rc, expected)
        return output if expected is not None else (rc, output)

    def close(self):
        try:
            USBJTAGSerialReset(self.port)()
        finally:
            self.port.close()
            self.log.close()


results = []
rf_frames = 0


def passed(name, **details):
    entry = {'name': name, 'result': 'PASS', **details}
    results.append(entry)
    print(json.dumps(entry), flush=True)


def air(sender, receiver, frame):
    global rf_frames
    sender.call('T', frame)
    received = receiver.call('R')
    assert received == frame, ('RF bytes differ', len(frame), len(received))
    rf_frames += 1
    return received


def fragment(sender, receiver, message, kind, order=None):
    receiver.call('F')
    token = secrets.token_bytes(8)
    count = (len(message) + 223) // 224
    order = list(range(count)) if order is None else order
    output = b''
    for index in order:
        frame = (b'NF\x01' + bytes([kind, index, count]) + len(message).to_bytes(2, 'big') +
                 token + message[index*224:(index+1)*224])
        rc, output = receiver.call('G', air(sender, receiver, frame), expected=None)
        assert rc in (0, -7), rc
    assert rc == 0 and output == message
    return output


def handshake(a, b, keys, duplicate=False):
    a.call('P', keys[1])
    b.call('P', keys[0])
    message = a.call('E')
    for number in range(1, 5):
        sender, receiver = (a, b) if number % 2 else (b, a)
        incoming = fragment(sender, receiver, message, number)
        message = receiver.call('E', incoming)
        if duplicate and number < 4:
            assert receiver.call('E', incoming) == message
    assert message == b''
    fingerprints = [a.call('O'), b.call('O')]
    assert len(fingerprints[0]) == 16 and fingerprints[0] == fingerprints[1]
    return fingerprints[0]


def encrypted(a, b, plain, control=False):
    sealed = a.call('C' if control else 'S', plain)
    assert len(sealed) == len(plain) + 40
    clear = b.call('D' if control else 'U', air(a, b, sealed))
    assert clear == plain
    return sealed


def campaign(a, b):
    keys = [a.call('I'), b.call('I')]
    assert all(len(k) == 65 and k[0] == 4 for k in keys) and keys[0] != keys[1]
    passed('distinct MCU-generated P256 public credentials')
    for board in (a, b):
        board.call('?', expected=-1)
        board.call('T', bytes(241), expected=-1)
        board.call('G', b'bad', expected=-1)
        board.call('O', expected=-14)
    passed('invalid commands, oversized RF and unauthenticated session creation rejected')
    fingerprint = handshake(a, b, keys, duplicate=True)
    passed('mutual EDHOC over LoRa with exact handshake retries', fingerprint=fingerprint.hex())
    a.call('L', expected=-12)
    accept = a.call('A')
    a.call('L', expected=-12)
    assert accept[:4] == b'NJ\x01\x01'
    encrypted(a, b, accept, True)
    ack = b.call('Q', accept)
    assert ack[:4] == b'NJ\x01\x02'
    encrypted(b, a, ack, True)
    a.call('V', ack)
    assert b.call('Q', accept) == ack
    a.call('V', ack)
    a.call('L')
    assert a.call('Z') == ack and b.call('Z') == ack
    passed('encrypted Join accept/commit, duplicate commit, actual Flash reopen')
    for sender, receiver in [(a, b), (b, a)]:
        for length in [1, 23, 24, 25, 63, 64, 65, 160, 199, 200]:
            encrypted(sender, receiver, bytes((i*13+length) % 256 for i in range(length)))
        sender.call('S', bytes(201), expected=-9)
    passed('bidirectional AES-CCM payload boundaries through maximum 240-byte RF frame', frames=20)
    for sender, receiver in [(a, b), (b, a)]:
        for i in range(100):
            encrypted(sender, receiver, i.to_bytes(4, 'big') + bytes([i])*60)
    passed('encrypted bidirectional soak', each_direction=100)
    pristine = a.call('S', bytes(range(200)))
    for index in range(len(pristine)):
        changed = bytearray(pristine)
        changed[index] ^= 1
        rc, data = b.call('U', air(a, b, bytes(changed)), expected=None)
        assert rc != 0 and not data, index
    assert b.call('U', air(a, b, pristine)) == bytes(range(200))
    for _ in range(3):
        rc, _ = b.call('U', air(a, b, pristine), expected=None)
        assert rc != 0
    passed('every byte tampered over RF rejected; original still usable; replay rejected', mutations=240)
    for sender, receiver in [(a, b), (b, a)]:
        control = sender.call('C', b'channel separation')
        rc, _ = receiver.call('U', air(sender, receiver, control), expected=None)
        assert rc != 0
        assert receiver.call('D', air(sender, receiver, control)) == b'channel separation'
        plain = sender.call('S', b'no reflection')
        # Peer reflects the received frame back over RF to the original sender.
        reflected = air(receiver, sender, air(sender, receiver, plain))
        rc, _ = sender.call('U', reflected, expected=None)
        assert rc != 0
    passed('control/data channel separation and reflected packets rejected')
    # Loss is intentionally withheld at the bench owner; retries use new counters.
    lost = a.call('S', b'withheld test frame')
    encrypted(a, b, b'withheld test frame')
    before = a.call('S', b'counter before reopen')
    a.call('K')
    after = encrypted(a, b, b'counter after reopen')
    assert int.from_bytes(after[24:29], 'big') > int.from_bytes(before[24:29], 'big') + 1
    passed('lost envelope retry and physical Flash counter reservation reopen')
    first = a.call('S', b'reorder one')
    second = a.call('S', b'reorder two')
    assert b.call('U', air(a, b, second)) == b'reorder two'
    assert b.call('U', air(a, b, first)) == b'reorder one'
    oldest = a.call('S', b'outside window')
    for _ in range(64):
        newest = a.call('S', b'advance test counter')
    assert b.call('U', air(a, b, newest)) == b'advance test counter'
    rc, _ = b.call('U', air(a, b, oldest), expected=None)
    assert rc != 0
    passed('in-window reordering accepted; outside replay window rejected')
    message = bytes(i % 256 for i in range(1024))
    fragment(a, b, message, 1, [4, 2, 2, 1, 3, 0])
    fragment(b, a, message, 2, [3, 1, 4, 0, 2])
    passed('1024-byte control reassembly over RF, reorder and duplicate', fragments=11)
    new_fingerprint = handshake(a, b, keys)
    assert new_fingerprint != fingerprint
    rc, _ = b.call('U', air(a, b, lost), expected=None)
    assert rc != 0
    encrypted(a, b, b'fresh session after rekey')
    encrypted(b, a, b'fresh reverse session')
    passed('fresh EDHOC rekey; previous-session ciphertext rejected')
    a.call('W')
    a.call('L', expected=-12)
    assert a.call('Z')[:4] == b'NJ\x01\x03'
    passed('revocation committed in physical Flash and peer policy denied')
    a.call('X')
    a.call('S', b'closed', expected=-1)
    passed('closed session refuses encryption')
    a.call('P', keys[0])  # Deliberately pin the wrong public key.
    b.call('P', keys[0])
    m1 = a.call('E')
    m2 = b.call('E', fragment(a, b, m1, 1))
    rc, _ = a.call('E', fragment(b, a, m2, 2), expected=None)
    assert rc != 0
    rc, _ = a.call('O', expected=None)
    assert rc != 0
    passed('wrong pinned peer credential fails handshake and prevents session creation')
    for board in [a, b]:
        board.call('X')
        health = board.call('H')
        stack, heap, minimum_heap = [int.from_bytes(health[i:i+4], 'big') for i in (0, 4, 8)]
        assert stack >= 4096 and heap > 32768 and minimum_heap > 16384
        passed('post-campaign stack/heap headroom', board=board.name,
               minimum_stack_free=stack, heap_free=heap, minimum_heap_free=minimum_heap)


def main():
    label = sys.argv[1]
    assert re.fullmatch(r'secure-20260908-[a-z0-9-]+', label)
    boards = []
    result = {'label': label, 'started': datetime.now(timezone.utc).isoformat()}
    try:
        for name in ('a', 'b'):
            boards.append(Board(name, label))
        campaign(*boards)
        result['result'] = 'PASS'
    except BaseException as error:
        result.update(result='FAIL', error=repr(error))
        raise
    finally:
        for board in boards:
            board.close()
        result.update(tests=results, rf_frames=rf_frames,
                      finished=datetime.now(timezone.utc).isoformat())
        with (EVIDENCE / f'{label}-result.json').open('x', encoding='utf-8') as output:
            json.dump(result, output, indent=2)


if __name__ == '__main__':
    main()
