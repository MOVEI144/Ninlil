#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
clang_bin=${CLANG:-clang}
temp=$(mktemp -d)
trap 'rm -rf "$temp"' EXIT
mkdir "$temp/corpus"
cp "$root"/tests/sim/manifests/*.txt "$temp/corpus/"
"$clang_bin" -std=c11 -Wall -Wextra -Wpedantic -Werror -Wshadow \
  -Wconversion -Wsign-conversion -Wformat=2 -Wundef -Wcast-align \
  -Wstrict-prototypes -Wmissing-prototypes -Wvla -fno-common \
  -g -fsanitize=fuzzer,address,undefined -fno-omit-frame-pointer \
  -I"$root/include" -I"$root/tests/sim" \
  "$root/tests/sim/fuzz_manifest.c" "$root/tests/sim/sim_manifest.c" \
  -o "$temp/fuzz_manifest"
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  "$temp/fuzz_manifest" "$temp/corpus" -seed=20260907 -runs=10000 \
  -max_len=8192 -timeout=5 -artifact_prefix="$temp/"
echo 'W02 manifest parser fuzz PASS (10000 executions, seed 20260907)'
