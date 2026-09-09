"""Build reference nodes with recorded source, configuration and image hashes."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('sdkconfig', type=Path)
    p.add_argument('roster', type=Path, help='Public trust header, or - for initial identity setup')
    p.add_argument('output', type=Path)
    p.add_argument('--nodes', type=int, nargs='+', default=[1, 2, 3])
    p.add_argument('--build-directory', type=Path, help='Parent directory for per-node ESP-IDF caches')
    p.add_argument('--sleep-probe', action='store_true', help='Bench only: retain sleep checkpoints in a separate NVS namespace')
    args = p.parse_args()
    if not args.nodes or len(set(args.nodes)) != len(args.nodes) or any(n < 1 or n > 65534 for n in args.nodes):
        p.error('Node IDs must be distinct values in 1..65534')
    root = Path(__file__).resolve().parents[2]
    idf = Path(os.environ['IDF_PATH']) / 'tools/idf.py'
    source = args.sdkconfig.read_text(encoding='utf-8')
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    roster = args.roster.resolve() if str(args.roster) != '-' else None
    paths = subprocess.check_output(['git', 'ls-files', '--cached', '--others', '--exclude-standard', '-z'], cwd=root).decode().split('\0')
    inputs = {root / n: digest(root / n) for n in paths if n and (root / n).is_file()
              and (n.startswith(('src/', 'include/', 'ports/', 'third_party/', 'cmake/', 'embedded/')) or n == 'CMakeLists.txt')}
    inputs[args.sdkconfig.resolve()] = digest(args.sdkconfig)
    if args.sleep_probe:
        inputs[root / 'tools/node_hil/sleep_probe.c'] = digest(root / 'tools/node_hil/sleep_probe.c')
    if roster:
        inputs[roster] = digest(roster)
    for node in args.nodes:
        folder = output / str(node)
        folder.mkdir()
        config = re.sub(r'^CONFIG_NINLIL_NODE_ID=.*\n?', '', source, flags=re.M)
        (folder / 'sdkconfig').write_text(config + f'\nCONFIG_NINLIL_NODE_ID={node}\n', encoding='utf-8')
        cache = args.build_directory.resolve() if args.build_directory else output / 'build'
        build = cache / str(node)
        command = [sys.executable, str(idf), '-B', str(build), '-D', 'SDKCONFIG='+str(folder / 'sdkconfig')]
        command += ['-D', 'NINLIL_NODE_ROSTER_HEADER='+(str(roster) if roster else '')]
        command += ['-D', 'NINLIL_SLEEP_PROBE='+('ON' if args.sleep_probe else 'OFF')]
        with (folder / 'build.log').open('w', encoding='utf-8') as log:
            subprocess.run(command + ['reconfigure', 'build'], cwd=root / 'embedded/esp32s3', stdout=log, stderr=subprocess.STDOUT, check=True)
        for src, dst in [('ninlil_m1.bin', 'app.bin'), ('ninlil_m1.elf', 'app.elf'),
                         ('bootloader/bootloader.bin', 'bootloader.bin'),
                         ('partition_table/partition-table.bin', 'partitions.bin'), ('config/sdkconfig.h', 'sdkconfig.h')]:
            shutil.copyfile(build / src, folder / dst)
        names = ('app.bin', 'app.elf', 'bootloader.bin', 'partitions.bin', 'sdkconfig', 'sdkconfig.h')
        (folder / 'hashes.json').write_text(json.dumps({n: digest(folder / n) for n in names}, indent=2)+'\n', encoding='utf-8')
        print(f'Built node {node}', flush=True)
    if any(digest(path) != value for path, value in inputs.items()):
        raise RuntimeError('Build inputs changed; artifacts are not accepted')
    (output / 'inputs.json').write_text(json.dumps({str(path): value for path, value in inputs.items()}, indent=2)+'\n', encoding='utf-8')


if __name__ == '__main__':
    main()
