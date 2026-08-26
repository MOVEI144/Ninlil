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
  "$root/tests/test_core.c"
  "$root/tests/test_diag_radio.c"
  "$root/tests/test_flash.c"
  "$root/tests/test_radio_delivery.c"
  "$root/tests/test_support.c"
  "$root/tests/test_support.h"
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
        ! -path "$root/include/ninlil_security_state.h" \
        ! -path "$root/ports/flash/ninlil_security_state.c" \
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
