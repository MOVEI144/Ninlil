"""Count first-party lines; retain and separately report unchanged vendor bodies."""
import difflib
import hashlib
import json
import pathlib
import subprocess
import sys

root = pathlib.Path(__file__).resolve().parent.parent
records = {r['path']: r for r in json.loads(
    (root / 'third_party/adapted/PROVENANCE.json').read_text(encoding='utf-8'))}
files = sys.stdin.read().splitlines()
if len(files) != len(set(files)):
    raise SystemExit('Duplicate source-ledger paths')
physical = first_party = inherited = 0
for name in files:
    path = pathlib.Path(name)
    if not path.is_absolute():
        path = root / path
    text = path.read_text(encoding='utf-8')
    lines = text.splitlines()
    total = sum(bool(line.strip()) for line in lines)
    physical += len(lines)
    relative = path.relative_to(root).as_posix()
    record = records.get(relative)
    if record:
        upstream = (root / record['upstream_path']).read_text(encoding='utf-8')
        if (hashlib.sha256(text.encode()).hexdigest() != record['sha256'] or
            hashlib.sha256(upstream.encode()).hexdigest() != record['upstream_lf_sha256']):
            raise SystemExit('Adapted/upstream digest mismatch: '+relative)
        # Apply the same formatting to both sides: reindentation of upstream
        # code is not an original implementation. Inputs remain unchanged.
        canonical = lambda value: subprocess.check_output(
            ['clang-format', '--style=file:'+str(root / '.clang-format')],
            input=value.encode()).decode().splitlines()
        old_lines, new_lines = canonical(upstream), canonical(text)
        own = sum(sum(bool(line.strip()) for line in new_lines[start:end])
                  for tag, _, _, start, end in difflib.SequenceMatcher(
                      None, old_lines, new_lines, autojunk=False).get_opcodes()
                  if tag != 'equal')
        assert 0 <= own <= total
        inherited += total - own
        first_party += own
    else:
        first_party += total
print(f'Physical lines: {physical}; unchanged vendor lines: {inherited}')
print(f'First-party nonblank lines: {first_party} / {int(sys.argv[1])}')
if first_party > int(sys.argv[1]):
    raise SystemExit('First-party line budget exceeded')
