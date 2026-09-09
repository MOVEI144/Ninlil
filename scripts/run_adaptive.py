"""Reproduce the bounded adaptation component matrix, not the full Ninlil SDK CI."""
from __future__ import annotations
import argparse
import hashlib
import json
import os
import platform
import shutil
import subprocess
from pathlib import Path


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("output", type=Path, help="New directory for builds and immutable logs")
    p.add_argument("--resume", action="store_true", help="Reuse build caches, preserving old logs in numbered attempts")
    p.add_argument("--jobs", type=int, default=4, choices=range(1, 9))
    args = p.parse_args()
    root = Path(__file__).resolve().parents[1]
    args.output.mkdir(parents=True, exist_ok=args.resume)
    output = args.output.resolve()
    attempts = [output/f"attempt-{i:03d}" for i in range(1, 1001)]
    logs = next((path for path in attempts if not path.exists()), None)
    if logs is None:
        raise ValueError("Attempt directory limit reached")
    logs.mkdir()
    report = {"fixture_warning": "Crypto, radio, Core and Coordinator boundaries are doubles in probe_pipeline; not a full SDK or RF gate",
              "scope": "adaptation components, production node/pump boundary models, seven-board protocol fixtures",
              "full_sdk": "NOT_RUN", "ESP_IDF": "NOT_RUN", "HIL": "NOT_RUN",
              "python": platform.python_version(), "runs": [], "sources": {}}
    sources = list((root/"include").glob("ninlil_*.h")) + list((root/"src").glob("ninlil_*.c"))
    sources += list((root/"tests").glob("test_*"))
    sources += [root/"include/ninlil.h", root/"scripts/run_adaptive.py", root/"cmake/adaptive.cmake", root/"tests/adaptive/CMakeLists.txt", root/"tools/node_hil/seven.py"]
    sources += list((root/"tests/probe_stub").glob("*.h"))
    sources += list((root/"cmake").glob("probe_*.cmake"))
    sources += [root/"CMakeLists.txt", root/"ports/esp32s3/ninlil_network_pump.c",
                root/"ports/esp32s3/ninlil_network_pump.h",
                root/"embedded/esp32s3/components/ninlil_network/CMakeLists.txt",
                root/"embedded/esp32s3/components/ninlil_network/Kconfig"]
    for path in sources:
        if path.is_file():
            report["sources"][str(path.relative_to(root))] = hashlib.sha256(path.read_bytes()).hexdigest()
    environment = os.environ.copy()
    environment["ASAN_OPTIONS"] = "detect_leaks=1:halt_on_error=1"
    environment["UBSAN_OPTIONS"] = "halt_on_error=1:print_stacktrace=1"
    passed = True
    for compiler in ("gcc", "clang"):
        resolved = shutil.which(compiler)
        if not resolved:
            report["runs"].append({"compiler": compiler, "result": "NOT_RUN", "reason": "compiler missing"})
            passed = False
            continue
        version = subprocess.run([resolved, "--version"], capture_output=True, text=True, check=True).stdout.splitlines()[0]
        for sanitize in (False, True):
            name = compiler + ("-sanitize" if sanitize else "")
            build = output/name
            entry = {"compiler": version, "sanitize": sanitize, "result": "PASS", "commands": []}
            commands = [
                ["cmake", "-S", str(root/"tests/adaptive"), "-B", str(build), "-G", "Ninja",
                 "-DCMAKE_BUILD_TYPE=Debug", "-DCMAKE_C_COMPILER="+resolved,
                 "-DNINLIL_SANITIZE="+("ON" if sanitize else "OFF")],
                ["cmake", "--build", str(build), "--parallel", str(args.jobs)],
                ["ctest", "--test-dir", str(build), "--output-on-failure", "--output-junit", str(logs/(name+".xml"))]]
            for index, command in enumerate(commands):
                path = logs/f"{name}-{index}.log"
                try:
                    proc = subprocess.run(command, cwd=root, env=environment, capture_output=True,
                                          text=True, timeout=180, check=False)
                    text, code = proc.stdout+proc.stderr, proc.returncode
                except (OSError, subprocess.TimeoutExpired) as error:
                    text, code = repr(error), -1
                path.write_text(text, encoding="utf-8")
                entry["commands"].append({"argv": command, "returncode": code, "log": path.name,
                                          "sha256": hashlib.sha256(path.read_bytes()).hexdigest()})
                if code:
                    entry["result"], passed = "FAIL", False
                    break
            report["runs"].append(entry)
    changed = [name for name, expected in report["sources"].items()
               if not (root/name).is_file() or
               hashlib.sha256((root/name).read_bytes()).hexdigest() != expected]
    report["changed_during_run"] = changed
    if changed:
        passed = False
    report["result"] = "PASS" if passed else "FAIL"
    (logs/"results.json").write_text(json.dumps(report, indent=2)+"\n", encoding="utf-8")
    print(json.dumps({"result": report["result"], "report": str(logs/"results.json"),
                      "scope": report["scope"], "full_sdk": "NOT_RUN", "HIL": "NOT_RUN"}, indent=2))
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
