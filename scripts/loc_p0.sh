#!/usr/bin/env bash
set -euo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
limit=${NINLIL_P0_LOC_LIMIT:-12000}
paths=(
  "$root/include/ninlil.h"
  "$root/include/ninlil_custody.h"
  "$root/include/ninlil_group.h"
  "$root/include/ninlil_leaf.h"
  "$root/include/ninlil_topology.h"
  "$root/src/ninlil.c"
  "$root/src/ninlil_authorization.c"
  "$root/src/ninlil_custody.c"
  "$root/src/ninlil_group.c"
  "$root/src/ninlil_internal.h"
  "$root/src/ninlil_journal.h"
  "$root/src/ninlil_leaf.c"
  "$root/src/ninlil_policy.c"
  "$root/src/ninlil_profile.c"
  "$root/src/ninlil_receive.c"
  "$root/src/ninlil_replay.c"
  "$root/src/ninlil_send.c"
  "$root/src/ninlil_storage.c"
  "$root/src/ninlil_topology.c"
  "$root/src/ninlil_wire.c"
  "$root/src/ninlil_wire.h"
  "$root/ports/posix/ninlil_journal.c"
  "$root/ports/flash/ninlil_flash_store.c"
  "$root/ports/flash/ninlil_flash_store.h"
  "$root/ports/flash/ninlil_flash_journal_file.c"
  "$root/ports/esp32s3/ninlil_flash_journal.c"
  "$root/tests/test_core.c"
  "$root/tests/test_flash.c"
  "$root/tests/test_operations.c"
  "$root/tests/test_radio_delivery.c"
  "$root/tests/test_support.c"
  "$root/tests/test_support.h"
  "$root/docs/P0_DELIVERY_CONTRACT_V2.md"
  "$root/docs/P0_OPERATIONAL_PROFILES_V1.md"
  "$root/docs/P0_IMPLEMENTATION.md"
  "$root/scripts/loc_p0.sh"
)

physical=0
nonblank=0
for file in "${paths[@]}"; do
  [[ -f "$file" ]] || continue
  physical=$((physical + $(wc -l < "$file")))
  nonblank=$((nonblank + $(awk 'NF { n++ } END { print n + 0 }' "$file")))
done
printf 'P0 contract physical lines: %d\n' "$physical"
printf 'P0 contract nonblank lines: %d / %d\n' "$nonblank" "$limit"
if ((nonblank > limit)); then
  echo "P0 contract line budget exceeded" >&2
  exit 1
fi
