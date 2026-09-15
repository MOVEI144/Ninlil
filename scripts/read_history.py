#!/usr/bin/env python3
"""Emit one hash-verified historical record from the pinned Git archive index."""
import csv
import hashlib
from pathlib import Path
import subprocess
import sys


def main() -> int:
    root = Path(__file__).resolve().parent.parent
    if len(sys.argv) != 2:
        print("usage: scripts/read_history.py PATH", file=sys.stderr)
        return 2
    with (root / "docs/evidence/ARCHIVE_2026-09-15.csv").open(newline="") as stream:
        entries = {row["path"]: row for row in csv.DictReader(stream)}
    entry = entries.get(sys.argv[1])
    if entry is None:
        print("path is not in the immutable history index", file=sys.stderr)
        return 2
    try:
        result = subprocess.run(
            ["git", "show", f"{entry['revision']}:{entry['path']}"],
            cwd=root, check=True, capture_output=True,
        )
    except subprocess.CalledProcessError:
        print("pinned Git object unavailable; fetch the documented revision first", file=sys.stderr)
        return 1
    content = result.stdout
    blob = hashlib.sha1(b"blob " + str(len(content)).encode() + b"\0" + content).hexdigest()
    if (len(content) != int(entry["bytes"]) or blob != entry["git_blob"] or
            hashlib.sha256(content).hexdigest() != entry["sha256"]):
        print("historical record integrity check failed", file=sys.stderr)
        return 1
    sys.stdout.buffer.write(content)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
