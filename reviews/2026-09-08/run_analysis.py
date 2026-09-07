"""Run GCC's analyzer through object compilation, preserving configured flags."""
import json
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[2]
build = Path(sys.argv[1]).resolve()
subprocess.run(["cmake", "-S", str(root), "-B", str(build),
                "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON"], check=True)
entries = json.loads((build / "compile_commands.json").read_text())
seen = set()
with tempfile.TemporaryDirectory(prefix="ninlil-review-analyzer-") as output:
    for entry in entries:
        file = Path(entry["file"]).resolve()
        relative = file.relative_to(root)
        if relative.parts[0] not in ("src", "ports") or file in seen:
            continue
        command = shlex.split(entry["command"])
        index = command.index("-o")
        command[index + 1] = str(Path(output) / (str(len(seen)) + ".o"))
        command.append("-fanalyzer")
        print("ANALYZE", relative, flush=True)
        subprocess.run(command, cwd=entry["directory"], check=True, timeout=60)
        seen.add(file)
    for name in ("ninlil_flash_journal.c", "ninlil_flash_admin.c"):
        file = root / "ports/esp32s3" / name
        command = ["gcc", "-std=c11", "-Wall", "-Wextra", "-Wpedantic", "-Werror",
                   "-Wconversion", "-Wsign-conversion", "-fanalyzer", "-DESP_PLATFORM=1"]
        command += ["-I" + str(root / include) for include in
                    ("include", "src", "ports/flash", "ports/esp32s3/include", "tests/esp_stub")]
        command += ["-c", str(file), "-o", str(Path(output) / name.replace(".c", ".o"))]
        print("ANALYZE", file.relative_to(root), flush=True)
        subprocess.run(command, cwd=root, check=True, timeout=60)
        seen.add(file)
print("GCC analyzer object-compilation PASS", len(seen), "first-party translation units", flush=True)
