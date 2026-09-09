"""Reproduce review witnesses against existing, locally built Ninlil libraries.

REPRODUCED confirms a defect in the reviewed revision, not a passing safety test.
No device, network, production source or default test registration is modified.
"""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument("build_root", type=Path)
args = parser.parse_args()
root = Path(__file__).resolve().parents[2]
directory = Path(__file__).resolve().parent
common = ["-std=c11", "-Wall", "-Wextra", "-Wpedantic", "-Werror",
          "-Wshadow", "-Wconversion", "-Wsign-conversion", "-Wformat=2",
          "-Wundef", "-Wcast-align", "-Wstrict-prototypes",
          "-Wmissing-prototypes", "-Wvla", "-fno-common"]
includes = ["-I" + str(root / p) for p in
            ["include", "src", "tests", "tests/esp_stub", "ports/flash", "ports/esp32s3"]]
includes += ["-isystem", str(root / "third_party/libedhoc/externals/mbedtls/include")]
environment = dict(os.environ, ASAN_OPTIONS="detect_leaks=1:halt_on_error=1",
                   UBSAN_OPTIONS="halt_on_error=1")
support = str(root / "tests/test_support.c")
for kind in ["gcc", "clang", "gcc-sanitize", "clang-sanitize"]:
    build = args.build_root.resolve() / kind
    compiler = "clang" if kind.startswith("clang") else "gcc"
    sanitize = ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"] if "sanitize" in kind else []
    library = lambda name: str(build / ("libninlil_" + name + ".a"))
    crypto = str(build / "third_party/libedhoc/externals/mbedtls/library/libmbedcrypto.a")
    targets = {
        "control": [library("control")],
        "storage": [library("flash_store")],
        "quota": [support, library("posix")],
        "contract": [library("control"), library("posix")],
        "pump": [support, str(root / "ports/esp32s3/ninlil_network_pump.c"),
                 library("routed"), library("secure"), library("security_state"),
                 library("control"), library("posix"), crypto],
    }
    print("CONFIGURATION", kind, flush=True)
    with tempfile.TemporaryDirectory(prefix="ninlil-review-") as output:
        for name, inputs in targets.items():
            executable = str(Path(output) / name)
            command = [compiler, *common, *sanitize, *includes,
                       str(directory / (name + "_probe.c")), *inputs,
                       "-o", executable]
            subprocess.run(command, cwd=root, check=True, env=environment)
            subprocess.run([executable], cwd=root, check=True, env=environment)
print("All review witnesses reproduced in four compiler/sanitizer configurations.", flush=True)
