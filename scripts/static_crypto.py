"""Analyze the first-party crypto bridge with its actual configured includes."""
import json
import pathlib
import shlex
import subprocess
import sys

root = pathlib.Path(__file__).resolve().parent.parent
build = pathlib.Path(sys.argv[1]).resolve()
subprocess.run(["cmake", "-S", str(root), "-B", str(build),
                "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON"], check=True)
expected = {(root / "ports/crypto/ninlil_edhoc.c").resolve(),
            (root / "ports/crypto/ninlil_psa.c").resolve(),
            (root / "ports/crypto/ninlil_identity.c").resolve(),
            (root / "ports/posix/ninlil_identity_file.c").resolve(),
            (root / "ports/flash/ninlil_identity_flash.c").resolve()}
expected.update(path.resolve() for path in (root / "src").glob("ninlil_node*.c"))
expected.update(path.resolve() for path in (root / "src").glob("ninlil_bulk*.c"))
expected.update((root / "src" / name).resolve()
                for name in ("ninlil_admission.c", "ninlil_setup.c", "ninlil_field_plan.c"))
seen = set()
for entry in json.loads((build / "compile_commands.json").read_text()):
    path = pathlib.Path(entry["file"]).resolve()
    if path not in expected:
        continue
    arguments = shlex.split(entry["command"])
    clean = []
    index = 0
    while index < len(arguments):
        if arguments[index] == "-o":
            index += 2
        elif arguments[index] == "-c":
            index += 1
        else:
            clean.append(arguments[index])
            index += 1
    clean += ["--analyze", "-Xanalyzer", "-analyzer-output=text",
              "-Xanalyzer", "-analyzer-werror"]
    subprocess.run(clean, cwd=entry["directory"], check=True)
    seen.add(path)
if seen != expected:
    raise SystemExit("crypto compile-database coverage mismatch")
print("EDHOC / PSA / identity / autonomous owner static analysis PASS")
