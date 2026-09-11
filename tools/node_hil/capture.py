"""Verify or recover a byte-exact historical capture from its committed Git source."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('run', type=int)
    parser.add_argument('--extension', action='store_true', help='Use the storage/bulk/radio evidence index')
    parser.add_argument('--result', action='store_true', help='Recover the structured extension result instead of console bytes')
    parser.add_argument('--output', type=Path, help='Create a new file; never overwrite')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    folder = '2026-09-08-extensions' if args.extension else '2026-09-08-autonomous'
    index = json.loads((root / 'docs/evidence' / folder / 'INDEX.json').read_text())
    suffix = 'json' if args.result else 'jsonl'
    matches = [r for r in index['records'] if r['path'].endswith(f'/run{args.run}.{suffix}')]
    if len(matches) != 1:
        parser.error('That run/kind is not present in the selected evidence index')
    record = matches[0]
    data = subprocess.check_output(['git', 'show', record['git_source']], cwd=root)
    if len(data) != record['bytes'] or hashlib.sha256(data).hexdigest() != record['sha256']:
        raise RuntimeError('Historical capture integrity mismatch')
    if args.output:
        with args.output.open('xb') as output:
            output.write(data)
    print('Verified', record['git_source'], record['sha256'])


if __name__ == '__main__':
    main()
