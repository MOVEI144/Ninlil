"""Bounded public/test-data USB console; never extracts private key material."""
import argparse
import json
import re
import time
from pathlib import Path
import serial
from serial.tools import list_ports


class Board:
    def __init__(self, node, log=None):
        from hardware import IDENTITIES
        ports = [p.device for p in list_ports.comports()
                 if p.serial_number == IDENTITIES[node] and p.vid == 0x303a
                 and p.pid == 0x1001]
        if len(ports) != 1:
            raise RuntimeError(f'Expected one verified board {node}')
        self.node, self.log = node, log
        self.port = serial.Serial(port=None, baudrate=115200, timeout=0.1, write_timeout=1)
        self.port.dtr = self.port.rts = False
        self.port.port = ports[0]
        self.port.open()

    def call(self, command, payload=b'', expected=0, timeout=5):
        if len(command) != 1 or len(payload) > 128:
            raise ValueError('Invalid bounded console request')
        self.port.write(f'{command} {payload.hex()}\n'.encode('ascii'))
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            raw = self.port.readline(259).decode('ascii', errors='replace').strip()
            if 'panic' in raw or 'Guru Meditation' in raw:
                raise RuntimeError(raw)
            match = re.fullmatch(r'NODE (.) (-?\d+)(?: ([0-9a-f]*))?', raw)
            if not match:
                continue
            rc, result = int(match[2]), bytes.fromhex(match[3] or '')
            record = {'time': time.time(), 'node': self.node, 'command': match[1],
                      'request': payload.hex() if match[1] == command else None, 'result': rc, 'response': result.hex()}
            if self.log:
                self.log.write(json.dumps(record)+'\n')
                self.log.flush()
            if match[1] != command:
                continue
            if expected is not None and rc != expected:
                raise RuntimeError(record)
            return result if expected is not None else (rc, result)
        raise TimeoutError(f'Board {self.node} command {command}')

    def close(self):
        self.port.close()


def main():
    from hardware import IDENTITIES
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('node', type=int, choices=IDENTITIES)
    p.add_argument('command')
    p.add_argument('payload', nargs='?', default='')
    p.add_argument('--output', type=Path)
    args = p.parse_args()
    board = Board(args.node)
    try:
        rc, data = board.call(args.command, bytes.fromhex(args.payload), expected=None)
        result = {'node': args.node, 'command': args.command, 'result': rc,
                  'response': data.hex()}
        if args.output:
            args.output.write_text(json.dumps(result, indent=2)+'\n')
        print(json.dumps(result))
    finally:
        board.close()


if __name__ == '__main__':
    main()
