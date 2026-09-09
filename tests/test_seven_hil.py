"""Protocol/cleanup fixture tests; no USB, RF, real Core or production evidence."""
from __future__ import annotations
import copy
import hashlib
import importlib.util
import json
import struct
import unittest
import tempfile
from pathlib import Path

spec = importlib.util.spec_from_file_location("seven", Path(__file__).parents[1]/"tools/node_hil/seven.py")
assert spec and spec.loader
seven = importlib.util.module_from_spec(spec)
spec.loader.exec_module(seven)


def manifest():
    return {"schema": 1, "scenario": "bidirectional", "mac": "airtime-drr",
            "source_commit": "ab"*20, "seconds": 120, "sequence": 100,
            "root": 1, "radio_profile_reviewed": True,
            "nodes": [{"address": i, "role": 4 if i == 1 else 3 if i in (2, 3) else 2,
                       "setup_revision": 1, "usb_serial": f"test-only-{i}",
                       "identity": bytes([i]*32).hex(), "public_key": (b"\x04"+bytes([i]*64)).hex(),
                       "firmware_sha256": "aa"*32, "firmware": "not-a-real-firmware.bin"}
                      for i in range(1, 8)]}


class Fixture:
    def __init__(self, m):
        self.m, self.time, self.messages, self.log = m, 0.0, {}, []
        self.running, self.records, self.closed = {}, {i: 0 for i in range(1, 8)}, set()
        self.root = self.record(1)
        self.bad_identity = self.start_timeout = self.no_receipt = self.stale = self.fail_stop = False
        self.direct = False
        self.expired = self.unready = False

    def record(self, address):
        n = self.m["nodes"][address-1]
        public = bytes.fromhex(n["identity"]+n["public_key"])
        return (b"NM\x01"+public[32:]+b"NJ\x01\x01"+public[:32]+bytes([1]*16)
                +struct.pack(">HQQIBB", address, 1, 1, 19 if n["role"] == 3 else 3, n["role"], 1)
                +b"\x01"+bytes(15)+bytes(8))

    def now(self):
        return self.time

    def sleep(self, seconds):
        self.time += seconds

    def factory(self, n):
        parent = self

        class Board:
            def __init__(self):
                self.node, self.response = n["address"], b""

            def close(self):
                parent.closed.add(self.node)

            def call(self, cmd, payload=b"", **kwargs):
                a = self.node
                parent.log.append((a, cmd, payload))
                if cmd == "I":
                    return bytes(97) if parent.bad_identity else bytes.fromhex(n["identity"]+n["public_key"])
                if cmd == "U":
                    if payload == b"\x04":
                        return struct.pack(">QBHHH", 1, 0, a, 1, len(self.response))
                    if payload[0] == 5:
                        self.response = parent.record(int.from_bytes(payload[1:], "big"))
                        return b""
                    if payload[0] == 3:
                        offset = int.from_bytes(payload[1:], "big")
                        return self.response[offset:offset+120]
                    raise AssertionError("No configuration upload permitted")
                if cmd == "H":
                    running = parent.running.get(a, False)
                    result = bytearray(96)
                    result[:3] = bytes([running, running, running])
                    result[6:8] = (7).to_bytes(2, "big")
                    result[10:12] = parent.records[a].to_bytes(2, "big")
                    return bytes(result)
                if cmd == "G":
                    parent.running[a] = True
                    if parent.start_timeout:
                        raise TimeoutError("Start committed but reply lost")
                    return b""
                if cmd == "X":
                    if parent.fail_stop:
                        raise TimeoutError("Stop reply absent")
                    parent.running[a] = False
                    if a == 2:
                        for msg in parent.messages.values():
                            if not msg[2]:
                                msg[2] = True
                                parent.records[msg[1]] += 1
                    return b""
                if cmd == "S":
                    target, seq = struct.unpack(">HI", payload)
                    mid = hashlib.sha256(struct.pack(">HHI", a, target, seq)).digest()[:16]
                    delayed = parent.m["scenario"] == "relay-stop" and seq == parent.m["sequence"]+12
                    if mid not in parent.messages:
                        parent.messages[mid] = [a, target, not delayed]
                        if not delayed and not parent.stale:
                            parent.records[target] += 1
                    return mid
                if cmd == "Q":
                    msg = parent.messages[payload]
                    return b"\x01\x05" if msg[2] and not parent.no_receipt else b"\x00\x00"
                if cmd == "L":
                    src, target = struct.unpack(">HH", payload)
                    path = [src, target] if parent.direct else [src, 2 if parent.running[2] else 3, target]
                    data = bytearray(66)
                    data[:8] = (1000).to_bytes(8, "big")
                    data[8:16] = (1).to_bytes(8, "big")
                    data[16:24] = (1000 if parent.expired else 61000).to_bytes(8, "big")
                    data[40] = 1 if parent.unready else 3
                    data[44] = len(path)
                    for i, address in enumerate(path):
                        data[45+2*i:47+2*i] = address.to_bytes(2, "big")
                    return bytes(data)
                raise AssertionError(f"Unexpected command {cmd}")
        return Board()

    def run(self):
        return seven.run(self.m, self.factory, lambda x: None, self.now, self.sleep)


class SevenTests(unittest.TestCase):
    def test_plan(self):
        m = manifest()
        seven.validate(m)
        work = seven.plan(m)
        self.assertEqual(len(work), 12)
        self.assertEqual(len(set(x[2] for x in work)), 12)

    def test_count_and_duplicates(self):
        for key in ("address", "usb_serial", "identity", "public_key"):
            m = manifest()
            m["nodes"][1][key] = m["nodes"][0][key]
            with self.assertRaises(ValueError): seven.validate(m)
        m = manifest(); m["nodes"].pop()
        with self.assertRaises(ValueError): seven.validate(m)

    def test_unknown_and_overflow(self):
        for key, value in (("schema", True), ("root", True), ("seconds", 601),
                           ("sequence", 2**32), ("mac", "auto-everything")):
            m = manifest(); m[key] = value
            with self.assertRaises(ValueError): seven.validate(m)
        with self.assertRaises(ValueError):
            json.loads('{"schema":1,"schema":2}', object_pairs_hook=seven.unique_object)

    def test_fanout_and_fanin(self):
        f = Fixture(manifest()); r = f.run()
        self.assertEqual(r["result"], "PASS", r)
        self.assertEqual(len(r["deliveries"]), 12)
        self.assertEqual(f.records[1], 6)
        self.assertEqual(len(f.closed), 7)
        self.assertFalse(any(f.running.values()))
        self.assertTrue(all(cmd not in ("P", "F", "R", "V") for _, cmd, _ in f.log))

    def test_preflight_never_starts_unverified_devices(self):
        f = Fixture(manifest()); f.bad_identity = True
        self.assertEqual(f.run()["result"], "FAIL")
        self.assertFalse(any(cmd in ("G", "X") for _, cmd, _ in f.log))

    def test_ambiguous_start_is_stopped(self):
        f = Fixture(manifest()); f.start_timeout = True
        self.assertEqual(f.run()["result"], "UNKNOWN")
        self.assertFalse(any(f.running.values()))
        self.assertEqual(sum(cmd == "G" for _, cmd, _ in f.log), 1)

    def test_missing_receipt_stays_unknown(self):
        f = Fixture(manifest()); f.no_receipt = True
        r = f.run()
        self.assertEqual(r["result"], "UNKNOWN")
        self.assertTrue(all(d["outcome"] == "ACTIVE" for d in r["deliveries"]))
        self.assertFalse(any(f.running.values()))

    def test_old_sequences_cannot_pass(self):
        f = Fixture(manifest()); f.stale = True
        self.assertEqual(f.run()["result"], "FAIL")

    def test_cleanup_failure_overrides_pass(self):
        f = Fixture(manifest()); f.fail_stop = True
        self.assertEqual(f.run()["result"], "FAIL")
        self.assertEqual(len(f.closed), 7)

    def test_relay_failover_requires_before_and_after_path(self):
        m = manifest(); m.update(scenario="relay-stop", stop_relay=2, recovery_target=4)
        f = Fixture(m); r = f.run()
        self.assertEqual(r["result"], "PASS", r)
        self.assertEqual(r["recovery"]["before"], (1, 2, 4))
        self.assertEqual(r["recovery"]["after"], (1, 3, 4))
        self.assertEqual(len(r["deliveries"]), 13)
        f = Fixture(copy.deepcopy(m)); f.direct = True
        self.assertEqual(f.run()["result"], "FAIL")
        for defect in ("expired", "unready"):
            f = Fixture(copy.deepcopy(m)); setattr(f, defect, True)
            self.assertEqual(f.run()["result"], "FAIL")

    def test_build_mac_provenance(self):
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory)
            image, config = folder/"app.bin", folder/"sdkconfig.h"
            image.write_bytes(b"fixture image, not firmware")
            config.write_text("#define CONFIG_NINLIL_ADAPTIVE_MAC_EXPERIMENTAL 1\n")
            image_hash = hashlib.sha256(image.read_bytes()).hexdigest()
            (folder/"hashes.json").write_text(json.dumps({"app.bin": image_hash,
                "sdkconfig.h": hashlib.sha256(config.read_bytes()).hexdigest()}))
            m = manifest()
            for node in m["nodes"]:
                node.update(firmware="app.bin", firmware_sha256=image_hash)
            seven.verify_artifacts(m, folder)
            m["mac"] = "legacy"
            with self.assertRaises(ValueError): seven.verify_artifacts(m, folder)
            m["mac"] = "airtime-drr"
            config.write_text("#define CONFIG_NINLIL_ADAPTIVE_MAC_EXPERIMENTAL 0\n")
            with self.assertRaises(ValueError): seven.verify_artifacts(m, folder)


if __name__ == "__main__":
    unittest.main(verbosity=2)
