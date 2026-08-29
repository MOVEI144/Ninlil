#!/usr/bin/env bash
set -euo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
limit=${NINLIL_M1_SOFTWARE_LOC_LIMIT:-7000}

paths=(
  "$root/include"
  "$root/src"
  "$root/ports/flash"
  "$root/ports/esp32s3"
  "$root/embedded/esp32s3"
  "$root/tests/test_diag_radio.c"
  "$root/tests/test_sx1262_hal.c"
  "$root/tests/test_sx1262_physical.c"
  "$root/tests/esp_stub"
  "$root/tests/m1"
  "$root/scripts/build_esp32s3.sh"
  "$root/scripts/check_esp_syntax.sh"
  "$root/scripts/check_sx126x_driver.sh"
  "$root/scripts/ci_m1.sh"
  "$root/scripts/fetch_sx126x_driver.sh"
  "$root/scripts/loc_m1_software.sh"
  "$root/scripts/static_analysis.sh"
  "$root/third_party/UPSTREAM.toml"
)

files=()
for path in "${paths[@]}"; do
  [[ -e "$path" ]] || continue
  if [[ -d "$path" ]]; then
    while IFS= read -r -d '' file; do
      files+=("$file")
    done < <(
      find "$path" -type f \
        \( -name '*.c' -o -name '*.h' -o -name 'CMakeLists.txt' -o \
           -name '*.sh' -o -name '*.toml' -o -name '*.csv' \) \
        ! -path "$root/third_party/sx126x_driver/src/*" \
        ! -path "$root/include/ninlil.h" \
        ! -path "$root/include/ninlil_custody.h" \
        ! -path "$root/include/ninlil_group.h" \
        ! -path "$root/include/ninlil_leaf.h" \
        ! -path "$root/include/ninlil_topology.h" \
        ! -path "$root/include/ninlil_security_state.h" \
        ! -path "$root/src/ninlil.c" \
        ! -path "$root/src/ninlil_authorization.c" \
        ! -path "$root/src/ninlil_custody.c" \
        ! -path "$root/src/ninlil_group.c" \
        ! -path "$root/src/ninlil_internal.h" \
        ! -path "$root/src/ninlil_journal.h" \
        ! -path "$root/src/ninlil_leaf.c" \
        ! -path "$root/src/ninlil_policy.c" \
        ! -path "$root/src/ninlil_profile.c" \
        ! -path "$root/src/ninlil_receive.c" \
        ! -path "$root/src/ninlil_send.c" \
        ! -path "$root/src/ninlil_storage.c" \
        ! -path "$root/src/ninlil_topology.c" \
        ! -path "$root/src/ninlil_wire.c" \
        ! -path "$root/src/ninlil_wire.h" \
        ! -path "$root/ports/flash/ninlil_flash_journal_file.c" \
        ! -path "$root/ports/flash/ninlil_flash_store.c" \
        ! -path "$root/ports/flash/ninlil_flash_store.h" \
        ! -path "$root/ports/flash/ninlil_security_state.c" \
        ! -path "$root/ports/esp32s3/ninlil_flash_admin.c" \
        ! -path "$root/ports/esp32s3/ninlil_flash_journal.c" \
        ! -path "$root/ports/esp32s3/ninlil_security_partitions.c" \
        ! -path "$root/ports/esp32s3/ninlil_security_partitions.h" \
        -print0
    )
  else
    files+=("$path")
  fi
done

if ((${#files[@]} == 0)); then
  echo "no M1 first-party files found" >&2
  exit 1
fi

physical=0
nonblank=0
for file in "${files[@]}"; do
  physical=$((physical + $(wc -l < "$file")))
  nonblank=$((nonblank + $(awk 'NF { n++ } END { print n + 0 }' "$file")))
done

printf 'M1 software physical lines: %d\n' "$physical"
printf 'M1 software nonblank lines: %d / %d\n' "$nonblank" "$limit"
if ((nonblank > limit)); then
  echo "M1 software line budget exceeded" >&2
  exit 1
fi
