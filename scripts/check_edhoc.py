"""Verify pinned dependency provenance and committed adaptation, without mutation."""
import difflib
import hashlib
import json
import pathlib
import subprocess

root = pathlib.Path(__file__).resolve().parent.parent
vendor = root / "third_party/libedhoc"
expected = "2b245e4a8aedb675ded18d085801e20a980e8e5c"
actual = subprocess.check_output(["git", "-C", str(vendor), "rev-parse", "HEAD"], text=True).strip()
if actual != expected:
    raise SystemExit("libedhoc revision mismatch")
subprocess.run(["git", "-C", str(vendor), "diff", "--exit-code", "--quiet"], check=True)
status = subprocess.check_output(["git", "-C", str(vendor), "submodule", "status", "--recursive"], text=True)
if any(line and line[0] != " " for line in status.splitlines()):
    raise SystemExit("submodule revision mismatch")
for record in json.loads((root / "third_party/adapted/PROVENANCE.json").read_text()):
    upstream = (root / record["upstream_path"]).read_text(encoding="utf-8")
    adapted = (root / record["path"]).read_text(encoding="utf-8")
    for text, key in [(upstream, "upstream_lf_sha256"), (adapted, "sha256")]:
        if hashlib.sha256(text.encode()).hexdigest() != record[key]:
            raise SystemExit("dependency digest mismatch: " + record["path"])
    patch = "".join(difflib.unified_diff(upstream.splitlines(True), adapted.splitlines(True),
                                      fromfile=record["upstream_path"], tofile=record["path"]))
    if patch != (root / record["patch"]).read_text(encoding="utf-8"):
        raise SystemExit("patch ledger mismatch")
print("libedhoc/submodule pins and committed compatibility patch ledger PASS")
