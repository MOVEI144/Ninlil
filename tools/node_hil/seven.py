"""Seven-board, explicit-identity HIL runner. Default: validate/print, no USB.

No flashing, provisioning, credential upload, erase, RF-profile change or fault
injection. --execute starts only seven verified, initially stopped devices and
stops every possibly-started device in finally. Never retries an ambiguous S/G.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import re
import struct
import time
from pathlib import Path
from typing import Any, Callable


def require(condition: bool, message: str) -> None:
    if not condition:
        raise ValueError(message)


def hex_bytes(value: Any, size: int, name: str) -> bytes:
    require(isinstance(value, str) and bool(re.fullmatch(rf"[0-9a-f]{{{size*2}}}", value)),
            f"{name}: expected {size} lowercase hex bytes")
    result = bytes.fromhex(value)
    require(any(result), f"{name}: zero is not a verified identity")
    return result


def integer(value: Any, low: int, high: int, name: str) -> int:
    require(type(value) is int and low <= value <= high, f"Invalid {name}")
    return value


def validate(m: dict[str, Any]) -> None:
    require(isinstance(m, dict) and type(m.get("schema")) is int and m["schema"] == 1,
            "Expected manifest schema 1")
    require(not (set(m) - {"schema", "scenario", "mac", "source_commit", "seconds", "sequence",
                          "root", "nodes", "radio_profile_reviewed", "stop_relay", "recovery_target", "closed_probes"}),
            "Unknown manifest field")
    integer(m.get("root"), 1, 65534, "root")
    require(m.get("scenario") in ("bidirectional", "relay-stop"), "Invalid scenario")
    require(m.get("mac") in ("legacy", "airtime-drr"), "Declare the firmware MAC build mode")
    require(type(m.get("closed_probes", False)) is bool, "closed_probes must be a boolean")
    hex_bytes(m.get("source_commit"), 20, "source_commit")
    integer(m.get("seconds"), 120, 570, "seconds")
    integer(m.get("sequence"), 1, 2**32-14, "fresh sequence base")
    nodes = m.get("nodes")
    require(isinstance(nodes, list) and len(nodes) == 7, "Exactly seven devices are required")
    seen = {key: set() for key in ("address", "usb_serial", "identity", "public_key")}
    roots = []
    for n in nodes:
        require(isinstance(n, dict), "Invalid node entry")
        require(set(n) == {"address", "role", "setup_revision", "usb_serial", "identity", "public_key",
                           "firmware_sha256", "firmware"}, "Unknown or missing node field")
        integer(n.get("address"), 1, 65534, "address")
        integer(n.get("role"), 1, 4, "role")
        integer(n.get("setup_revision"), 1, 2**64-1, "setup revision")
        require(isinstance(n.get("usb_serial"), str) and 0 < len(n["usb_serial"]) <= 128,
                "Record the actual USB serial, not a port-order guess")
        hex_bytes(n.get("identity"), 32, "identity")
        require(hex_bytes(n.get("public_key"), 65, "public_key")[0] == 4, "Expected uncompressed P-256 key")
        hex_bytes(n.get("firmware_sha256"), 32, "firmware_sha256")
        require(isinstance(n.get("firmware"), str) and n["firmware"], "Record the firmware artifact path")
        for key, values in seen.items():
            require(n[key] not in values, f"Duplicate {key}")
            values.add(n[key])
        if n["role"] == 4:
            roots.append(n["address"])
    require(len(roots) == 1 and roots[0] == m.get("root"), "Exactly one declared Root")
    if m["scenario"] == "relay-stop":
        relays = {n["address"] for n in nodes if n["role"] == 3}
        require(m.get("stop_relay") in relays, "stop_relay must be a powered Relay")
        require(m.get("recovery_target") in seen["address"] - {m["root"], m["stop_relay"]},
                "Invalid recovery target")


def plan(m: dict[str, Any]) -> list[tuple[int, int, int]]:
    peers = sorted(n["address"] for n in m["nodes"] if n["address"] != m["root"])
    pairs = [(m["root"], p) for p in peers] + [(p, m["root"]) for p in peers]
    return [(a, b, m["sequence"]+i) for i, (a, b) in enumerate(pairs)]


def status(board: Any) -> dict[str, int]:
    data = board.call("H")
    require(len(data) in (40, 72, 96), "Malformed H status")
    return {"running": data[0], "joined": data[1], "clock": data[2], "injection": data[3],
            "members": int.from_bytes(data[6:8], "big"),
            "custody": int.from_bytes(data[8:10], "big"),
            "records": int.from_bytes(data[10:12], "big"),
            "fault": int.from_bytes(data[36:40], "big")}


def setup(board: Any) -> tuple[int, int, int, int, int]:
    data = board.call("U", b"\x04")
    require(len(data) == 15, "Malformed setup status")
    result = struct.unpack(">QBHHH", data)
    require(result[1] <= 1 and result[4] <= 299, "Invalid setup values")
    return result


def member(board: Any, address: int) -> bytes:
    board.call("U", b"\x05" + struct.pack(">H", address))
    length = setup(board)[4]
    require(160 <= length <= 224, "Malformed public member length")
    result = b""
    while len(result) < length:
        chunk = board.call("U", b"\x03" + struct.pack(">H", len(result)))
        require(0 < len(chunk) <= min(120, length-len(result)), "Malformed public member chunk")
        result += chunk
    require(result[:4] == b"NM\x01\x04" and result[68:72] == b"NJ\x01\x01"
            and len(result) == 160 + 8*result[143]
            and result[144:160] == b"\x01" + bytes(15), "Invalid public member record")
    require(int.from_bytes(result[120:122], "big") == address, "Wrong member address")
    return result


def preflight(board: Any, n: dict[str, Any], root: int) -> bytes:
    public = board.call("I")
    require(public == bytes.fromhex(n["identity"]+n["public_key"]), "Physical device identity/key mismatch")
    revision, autorun, local, saved_root, _ = setup(board)
    require((revision, autorun, local, saved_root) == (n["setup_revision"], 0, n["address"], root),
            "Setup changed, autorun armed, or wrong address/Root; no devices started")
    s = status(board)
    require(not s["running"] and not s["fault"] and not s["injection"],
            "Device must already be stopped, fault-free and without injected loss")
    own = member(board, local)
    require(own[72:104] == public[:32] and own[3:68] == public[32:] and own[142] == n["role"],
            "Stored role/identity does not match manifest")
    require(int.from_bytes(own[138:142], "big") & 3 == 3, "Bidirectional application capability is required")
    return member(board, root)


def route(board: Any, source: int, target: int) -> tuple[int, ...]:
    data = board.call("L", struct.pack(">HH", source, target))
    require(len(data) == 66 and 2 <= data[44] <= 5, "No observed local route")
    # Byte 40 is local_ready (APPLIED | EFFECTIVE), not the plan phase.
    # Reference node_main.c emits lease/current-epoch/expiry at 0/8/16.
    lease, epoch, expiry = struct.unpack(">QQQ", data[:24])
    require(data[40] == 3 and epoch != 0 and lease != 0 and
            lease < expiry <= lease + 60000, "Route not locally effective or lease expired")
    path = tuple(int.from_bytes(data[45+2*i:47+2*i], "big") for i in range(data[44]))
    require(len(set(path)) == len(path) and all(0 < x < 65535 for x in path)
            and path[0] == source and path[-1] == target, "Malformed or mismatched path")
    return path


def run(m: dict[str, Any], factory: Callable, emit: Callable,
        now: Callable = time.monotonic, sleep: Callable = time.sleep) -> dict[str, Any]:
    validate(m)
    result: dict[str, Any] = {"result": "UNKNOWN", "scope": "seven-MCU reference application delivery",
        "physical_range_claim": False, "field_acceptance": False, "firmware_attested_by_USB": False,
        "mac_from_manifest": m["mac"], "closed_probes_from_manifest": m.get("closed_probes", False), "source_commit": m["source_commit"], "deliveries": [],
        "errors": [], "cleanup": []}
    boards, controlled = {}, set()
    expected, baseline = {}, {}
    try:
        root_record = None
        for n in m["nodes"]:
            board = factory(n)
            boards[n["address"]] = board
            root = preflight(board, n, m["root"])
            require(root_record is None or root == root_record, "Different Root credentials in the seven-board set")
            root_record = root
        # A run never sends P, upload/revoke/erase commands or changes a radio profile.
        deadline = now() + m["seconds"]
        for address, board in boards.items():
            controlled.add(address)  # G may succeed even if its reply is lost.
            board.call("G", struct.pack(">I", (m["seconds"]+20)*1000))
        while True:
            snapshots = {a: status(b) for a, b in boards.items()}
            emit({"phase": "join", "nodes": snapshots})
            require(all(s["running"] and not s["fault"] and not s["injection"] for s in snapshots.values()), "Owner fault/start failure")
            if all(s["joined"] and s["clock"] and s["members"] == 7 for s in snapshots.values()):
                if now() > deadline:
                    raise TimeoutError("Join observed after deadline")
                baseline = {a: s["records"] for a, s in snapshots.items()}
                expected = {a: 0 for a in boards}
                require(all(v <= 65520 for v in baseline.values()), "Application ledger count lacks test headroom")
                break
            if now() >= deadline:
                raise TimeoutError("Seven-node Join/clock not observed before deadline")
            sleep(min(1.0, deadline-now()))
        work = plan(m)
        for source, target, sequence in work:
            message = boards[source].call("S", struct.pack(">HI", target, sequence))
            require(len(message) == 16 and any(message), "Invalid submission message ID")
            require(all(d["id"] != message.hex() for d in result["deliveries"]), "Reused message ID")
            result["deliveries"].append({"source": source, "target": target, "sequence": sequence,
                                         "id": message.hex(), "outcome": "ACTIVE"})
            expected[target] += 1
        def wait(deliveries: list[dict[str, Any]]) -> None:
            while True:
                for d in deliveries:
                    if d["outcome"] == "SATISFIED/APPLICATION_ACCEPTED":
                        continue
                    evidence = boards[d["source"]].call("Q", bytes.fromhex(d["id"]))
                    require(len(evidence) == 2 and max(evidence) <= 5, "Malformed delivery evidence")
                    if evidence == b"\x01\x05":
                        d["outcome"] = "SATISFIED/APPLICATION_ACCEPTED"
                    elif evidence[0] != 0:
                        raise ValueError(f"Unexpected terminal delivery evidence {evidence.hex()}")
                snaps = {a: status(b) for a, b in boards.items() if a in controlled}
                emit({"phase": "delivery", "nodes": snaps, "messages": deliveries})
                require(all(s["running"] and not s["fault"] and not s["injection"] for s in snaps.values()), "Owner fault during delivery")
                if all(d["outcome"] == "SATISFIED/APPLICATION_ACCEPTED" for d in deliveries):
                    if now() > deadline:
                        raise TimeoutError("Evidence observed after deadline")
                    break
                if now() >= deadline:
                    raise TimeoutError("Required end-to-end evidence not observed; ownership remains active")
                sleep(min(1.0, deadline-now()))
        wait(result["deliveries"])
        normal_final = {a: status(b) for a, b in boards.items()}
        require(all(normal_final[a]["records"] == baseline[a]+expected[a] for a in boards),
                "Normal-phase ledger growth mismatch; old sequences are not new delivery")
        result["normal_final_status"] = normal_final
        if m["scenario"] == "relay-stop":
            source, target, stopped = m["root"], m["recovery_target"], m["stop_relay"]
            before = route(boards[source], source, target)
            require(before[0] == source and before[-1] == target and stopped in before[1:-1],
                    "Selected Relay is not on the observed route; cannot claim failover")
            message = boards[source].call("S", struct.pack(">HI", target, m["sequence"]+12))
            require(len(message) == 16 and any(message), "Invalid recovery message ID")
            require(all(d["id"] != message.hex() for d in result["deliveries"]), "Reused recovery ID")
            initial = boards[source].call("Q", message)
            require(len(initial) == 2 and initial[0] == 0 and initial[1] <= 5,
                    "Recovery message already terminal or malformed before stop")
            boards[stopped].call("X", timeout=3)
            require(not status(boards[stopped])["running"], "Relay stop not observed")
            controlled.remove(stopped)
            recovery = {"source": source, "target": target, "sequence": m["sequence"]+12,
                        "id": message.hex(), "outcome": "ACTIVE"}
            result["deliveries"].append(recovery)
            expected[target] += 1
            wait([recovery])
            after = route(boards[source], source, target)
            require(after[0] == source and after[-1] == target and stopped not in after,
                    "No alternative effective route observed")
            result["recovery"] = {"before": before, "after": after, "kind": "USB stopped owner, NOT power cut"}
        final = {a: status(b) for a, b in boards.items()}
        for address in controlled:
            require(final[address]["records"] == baseline[address]+expected[address],
                    "Receiver ledger growth mismatch: stale sequence, missing or unrelated work")
        result["initial_records"], result["final_status"] = baseline, final
        result["result"] = "PASS"
    except Exception as error:
        result["result"] = "UNKNOWN" if isinstance(error, (TimeoutError, OSError)) else "FAIL"
        result["errors"].append(f"{type(error).__name__}: {error}")
    finally:
        for address, board in boards.items():
            try:
                if address in controlled:
                    board.call("X", timeout=3)
                    require(not status(board)["running"] and not setup(board)[1], "Stop/disarm not verified")
                result["cleanup"].append({"address": address, "stopped_or_untouched": True})
            except Exception as error:
                result["errors"].append(f"cleanup {address}: {error}")
                result["result"] = "FAIL"
            finally:
                try:
                    board.close()
                except Exception as error:
                    result["errors"].append(f"close {address}: {error}")
                    result["result"] = "FAIL"
    return result


def unique_object(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result = {}
    for key, value in pairs:
        require(key not in result, f"Duplicate JSON key: {key}")
        result[key] = value
    return result


def inventory() -> dict[str, Any]:
    """Read public identity/setup only. Does not create an executable manifest."""
    from manage import Device
    from serial.tools import list_ports
    ports = [p for p in list_ports.comports() if p.vid == 0x303a and p.pid == 0x1001]
    report: dict[str, Any] = {"result": "READ_ONLY", "USB_candidates": len(ports),
                             "RF_started": False, "nodes": []}
    require(len(ports) == 7, "Connect/select exactly seven boards before inventory; no console opened")
    for port in ports:
        row: dict[str, Any] = {"port": port.device, "usb_serial": port.serial_number}
        board = None
        try:
            board = Device(port.device)
            board.port.write_timeout = 1
            public = board.call("I")
            require(len(public) == 97 and public[32] == 4, "Unprovisioned/invalid public identity")
            revision, autorun, local, root, _ = setup(board)
            own = member(board, local)
            row.update(identity=public[:32].hex(), public_key=public[32:].hex(),
                       address=local, root=root, setup_revision=revision,
                       role=own[142], autorun=bool(autorun), status=status(board))
        except Exception as error:
            row["error"] = f"{type(error).__name__}: {error}"
        finally:
            if board:
                board.close()
        report["nodes"].append(row)
    return report


def verify_artifacts(m: dict[str, Any], manifest_directory: Path) -> None:
    """Check build.py's local image/config hashes; this is NOT device attestation."""
    for node in m["nodes"]:
        image = manifest_directory / node["firmware"]
        require(0 < image.stat().st_size <= 16*1024*1024, "Invalid firmware artifact size")
        require(hashlib.sha256(image.read_bytes()).hexdigest() == node["firmware_sha256"],
                "Firmware artifact hash mismatch")
        config, ledger = image.parent / "sdkconfig.h", image.parent / "hashes.json"
        require(config.stat().st_size <= 1024*1024 and ledger.stat().st_size <= 65536,
                "Build provenance exceeds bounded size")
        hashes = json.loads(ledger.read_text(encoding="utf-8"), object_pairs_hook=unique_object)
        require(isinstance(hashes, dict) and hashes.get("app.bin") == node["firmware_sha256"]
                and hashes.get("sdkconfig.h") == hashlib.sha256(config.read_bytes()).hexdigest(),
                "Use matching app.bin, sdkconfig.h and hashes.json from build.py")
        definitions = re.findall(r"^#define CONFIG_NINLIL_ADAPTIVE_MAC_EXPERIMENTAL\s+(\S+)\s*$",
                                 config.read_text(encoding="utf-8"), re.M)
        require(definitions in ([], ["1"]), "Unexpected or duplicate experimental MAC definition")
        require(bool(definitions) == (m["mac"] == "airtime-drr"), "MAC label disagrees with build")
        measurements = re.findall(r"^#define CONFIG_NINLIL_CLOSED_PROBES_EXPERIMENTAL\s+(\S+)\s*$",
                                  config.read_text(encoding="utf-8"), re.M)
        require(measurements in ([], ["1"]), "Unexpected or duplicate closed-probe definition")
        require(bool(measurements) == m.get("closed_probes", False),
                "Closed-probe label disagrees with build")


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("manifest", type=Path, nargs="?")
    p.add_argument("--inventory", action="store_true", help="Read public identity/setup of seven connected boards, no RF start")
    p.add_argument("--execute", action="store_true", help="Explicitly start the verified seven-board campaign")
    p.add_argument("--evidence", type=Path)
    args = p.parse_args()
    if args.inventory:
        require(not args.execute and args.manifest is None, "Inventory is separate from execution")
        print(json.dumps(inventory(), indent=2))
        return 0
    require(args.manifest is not None, "A reviewed manifest is required")
    require(args.manifest.stat().st_size <= 65536, "Manifest exceeds bounded size")
    m = json.loads(args.manifest.read_text(encoding="utf-8"), object_pairs_hook=unique_object)
    validate(m)
    if not args.execute:
        print(json.dumps({"result": "NOT_RUN", "plan": plan(m), "scenario": m["scenario"],
                          "required_boards": 7, "USB_opened": False}, indent=2))
        return 0
    require(args.evidence is not None, "--evidence is required for execution")
    require(m.get("radio_profile_reviewed") is True, "An operator must review the unchanged RF build/profile")
    verify_artifacts(m, args.manifest.parent)
    args.evidence.mkdir(parents=True, exist_ok=False)
    (args.evidence / "manifest.json").write_text(json.dumps(m, indent=2)+"\n", encoding="utf-8")
    from manage import Device
    from serial.tools import list_ports
    with (args.evidence / "console.jsonl").open("x", encoding="utf-8") as console_log, \
            (args.evidence / "trace.jsonl").open("x", encoding="utf-8") as trace:
        def factory(n: dict[str, Any]) -> Any:
            ports = [x.device for x in list_ports.comports() if x.serial_number == n["usb_serial"]
                     and x.vid == 0x303a and x.pid == 0x1001]
            require(len(ports) == 1, "Expected exactly one device with the recorded USB serial")
            board = Device(ports[0])
            try:
                board.node, board.log = n["address"], console_log
                board.port.write_timeout = 1
                return board
            except Exception:
                board.close()
                raise
        def emit(record: dict[str, Any]) -> None:
            require(trace.tell() < 16*1024*1024 and console_log.tell() < 16*1024*1024, "Evidence size limit")
            trace.write(json.dumps(record, sort_keys=True)+"\n")
            trace.flush()
        result = run(m, factory, emit)
    result["evidence_sha256"] = {name: hashlib.sha256((args.evidence/name).read_bytes()).hexdigest()
                                  for name in ("manifest.json", "console.jsonl", "trace.jsonl")}
    (args.evidence / "result.json").write_text(json.dumps(result, indent=2)+"\n", encoding="utf-8")
    print(json.dumps(result, indent=2))
    return 0 if result["result"] == "PASS" else 2


if __name__ == "__main__":
    raise SystemExit(main())
