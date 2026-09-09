"""Exit the first monitor process while A owns pending work and B is in ROM."""
import hashlib
import json
import time
from datetime import datetime, timezone
from pathlib import Path
from esptool.cmds import detect_chip
from serial.tools import list_ports

root = Path(__file__).resolve().parents[2] / '.verify-m1-evidence'
assert 'FLASH_VERIFIED variant=fault-a-init' in (root/'fault-a-init-r1-flash.log').read_text()
ports = [p for p in list_ports.comports() if p.device == 'COM3']
assert len(ports) == 1 and ports[0].serial_number == 'E0:72:A1:F7:FF:0C'
esp = detect_chip('COM3', baud=115200, connect_mode='no-reset', connect_attempts=3)
try:
    assert bytes(esp.read_mac()).hex() == 'e072a1f7ff0c'
    port = esp._port
    port.timeout = 0.2
    port.set_buffer_size(rx_size=1048576, tx_size=65536)
    port.reset_input_buffer()
    esp.hard_reset()
    raw = b''
    deadline = time.monotonic() + 20
    with (root/'fault-monitor-restart-pre-a.log').open('xb') as output:
        while time.monotonic() < deadline:
            data = port.read(min(max(port.in_waiting, 1),4096))
            raw += data
            assert len(raw) <= 1048576
            output.write(data)
            output.flush()
            if b'HIL_SUBMIT campaign=2026090704 seq=1 ' in raw and b'HIL_LINK_SENT bytes=52' in raw:
                break
    assert b'App version:      3254ae3' in raw
    assert b'HIL_SUBMIT campaign=2026090704 seq=1 ' in raw
    assert b'HIL_LINK_SENT bytes=52' in raw and b'HIL_SATISFIED' not in raw
    result = {'result':'PENDING_CAPTURED', 'end_action':'close monitor only; no reset',
              'log_sha256':hashlib.sha256(raw).hexdigest(),
              'utc':datetime.now(timezone.utc).isoformat()}
    with (root/'fault-monitor-restart-pre-a.json').open('x') as output:
        json.dump(result,output,indent=2)
    print(json.dumps(result),flush=True)
finally:
    # Deliberately keep the application running when this Python process exits.
    esp._port.close()
