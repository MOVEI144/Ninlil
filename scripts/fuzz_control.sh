#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
temp=$(mktemp -d)
trap 'rm -rf "$temp"' EXIT
"${CLANG:-clang}" -std=c11 -Wall -Wextra -Werror -Wconversion -Wsign-conversion \
  -g -fsanitize=fuzzer,address,undefined -fno-omit-frame-pointer \
  -I"$root/include" "$root/tests/fuzz_control.c" \
  "$root/src/ninlil_join.c" "$root/src/ninlil_join_wire.c" "$root/src/ninlil_authorization.c" \
  "$root/src/ninlil_network.c" "$root/src/ninlil_network_route.c" "$root/src/ninlil_network_wire.c" \
  "$root/src/ninlil_relay.c" \
  -o "$temp/fuzz_control"
mkdir "$temp/corpus"
python3 - "$temp/corpus" <<'PY'
import pathlib, struct, sys
root = pathlib.Path(sys.argv[1])
j = bytearray(100); j[:4] = b'NJ\x01\x01'; j[4:36] = bytes([1])*32; j[36:52] = bytes([2])*16
struct.pack_into('>HQQI', j, 52, 1, 1, 1, 3); j[74:76] = bytes([2,1]); j[76:92] = bytes([3])*16
struct.pack_into('>HHHBB', j, 92, 256, 64, 16, 3, 15); (root/'join').write_bytes(j)
p = bytearray(98); p[:7] = b'NP\x01\x03\x02\x03\x03'
struct.pack_into('>QQIIQ', p, 8, 1, 50000, 1, 100, 1000)
struct.pack_into('>HQHQ', p, 48, 1, 1, 2, 1); (root/'plan').write_bytes(p)
r = bytearray(88); r[:6] = b'NR\x01\x00\x03\x02'
struct.pack_into('>HHHHQ', r, 6, 40, 1, 3, 2, 0); struct.pack_into('>Q', r, 18, 1)
r[26:42] = bytes([4])*16; (root/'relay').write_bytes(r)
r2 = r[:48] + bytes(8) + r[48:]; r2[2] = 2
(root/'relay-v2').write_bytes(r2); (root/'long').write_bytes(bytes(1024))
PY
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  "$temp/fuzz_control" "$temp/corpus" -seed=20260907 -runs=10000 -max_len=1024 \
  -timeout=5 -artifact_prefix="$temp/"
echo 'Join/plan/Relay v1-v2 fuzz PASS (10000 executions)'
