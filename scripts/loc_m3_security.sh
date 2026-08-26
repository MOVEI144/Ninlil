#!/usr/bin/env bash
set -euo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
limit=${NINLIL_M3_SECURITY_LOC_LIMIT:-3500}
files=(
  "$root/include/ninlil_security_state.h"
  "$root/ports/flash/ninlil_security_state.c"
  "$root/ports/esp32s3/ninlil_security_partitions.h"
  "$root/ports/esp32s3/ninlil_security_partitions.c"
  "$root/tests/test_security_state.c"
  "$root/tests/test_security_partitions.c"
  "$root/docs/M3_3_SECURITY_STATE.md"
  "$root/docs/M3_3_ACCEPTANCE.md"
  "$root/scripts/loc_m3_security.sh"
  "$root/scripts/ci_m3.sh"
  "$root/tests/m3/CMakeLists.txt"
)

physical=0
nonblank=0
for file in "${files[@]}"; do
  [[ -f "$file" ]] || continue
  physical=$((physical + $(wc -l < "$file")))
  nonblank=$((nonblank + $(awk 'NF { n++ } END { print n + 0 }' "$file")))
done
printf 'M3.3 security-state physical lines: %d\n' "$physical"
printf 'M3.3 security-state nonblank lines: %d / %d\n' "$nonblank" "$limit"
if ((nonblank > limit)); then
  echo "M3.3 security-state line budget exceeded" >&2
  exit 1
fi
