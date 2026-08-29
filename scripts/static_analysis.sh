#!/usr/bin/env bash
set -euo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
gcc_bin=${1:-${GCC:-gcc}}
clang_bin=${2:-${CLANG:-clang}}
common=(
  -std=c11
  -I"$root/include"
  -I"$root/src"
  -I"$root/ports/flash"
  -I"$root/ports/esp32s3"
  -I"$root/ports/esp32s3/include"
  -I"$root/tests/esp_stub"
  -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion
  -Wformat=2 -Wundef -Wcast-align -Wstrict-prototypes
  -Wmissing-prototypes -Wvla -fno-common
)
host_sources=(
  "$root/src/ninlil.c"
  "$root/src/ninlil_authorization.c"
  "$root/src/ninlil_policy.c"
  "$root/src/ninlil_profile.c"
  "$root/src/ninlil_receive.c"
  "$root/src/ninlil_send.c"
  "$root/src/ninlil_storage.c"
  "$root/src/ninlil_wire.c"
  "$root/src/ninlil_diag.c"
  "$root/src/ninlil_radio.c"
  "$root/src/ninlil_rf_profile.c"
  "$root/ports/flash/ninlil_flash_store.c"
  "$root/ports/flash/ninlil_security_state.c"
  "$root/ports/flash/ninlil_flash_journal_file.c"
  "$root/ports/posix/ninlil_journal.c"
)
esp_sources=(
  "$root/ports/esp32s3/ninlil_sx1262_hal.c"
  "$root/ports/esp32s3/ninlil_sx1262_radio.c"
  "$root/ports/esp32s3/ninlil_flash_journal.c"
  "$root/ports/esp32s3/ninlil_flash_admin.c"
  "$root/ports/esp32s3/ninlil_security_partitions.c"
)

"$gcc_bin" "${common[@]}" -fanalyzer -fsyntax-only "${host_sources[@]}"
"$gcc_bin" "${common[@]}" -DESP_PLATFORM=1 -fanalyzer -fsyntax-only \
  "${esp_sources[@]}"

for source in "${host_sources[@]}"; do
  "$clang_bin" "${common[@]}" --analyze \
    -Xanalyzer -analyzer-output=text "$source"
done
for source in "${esp_sources[@]}"; do
  "$clang_bin" "${common[@]}" -DESP_PLATFORM=1 --analyze \
    -Xanalyzer -analyzer-output=text "$source"
done

echo "Ninlil static analysis PASS"
