#!/usr/bin/env bash
set -euo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
common=(
  -std=c11 -fsyntax-only
  -I"$root/include"
  -I"$root/tests/esp_stub"
  -I"$root/src"
  -I"$root/ports/flash"
  -I"$root/ports/esp32s3"
  -I"$root/ports/esp32s3/include"
  -DESP_PLATFORM=1
  -Wall -Wextra -Wpedantic -Werror -Wshadow -Wconversion
  -Wsign-conversion -Wformat=2 -Wundef -Wcast-align -Wvla
  -Wstrict-prototypes -Wmissing-prototypes -fno-common
)
platform_sources=(
  "$root/ports/esp32s3/ninlil_sx1262_hal.c"
  "$root/ports/esp32s3/ninlil_sx1262_radio.c"
  "$root/ports/esp32s3/ninlil_flash_journal.c"
  "$root/ports/esp32s3/ninlil_flash_admin.c"
  "$root/ports/esp32s3/ninlil_security_partitions.c"
)
app="$root/embedded/esp32s3/main/app_main.c"
base_config=(
  -DCONFIG_NINLIL_NODE_ID=1
  -DCONFIG_NINLIL_PEER_ID=2
  -DCONFIG_NINLIL_RADIO_INIT_CYCLES=1
  -DCONFIG_NINLIL_DIAGNOSTIC_PING_COUNT=1000
  -DCONFIG_NINLIL_DELIVERY_MESSAGE_COUNT=100
  -DCONFIG_ESP_MAIN_TASK_STACK_SIZE=16384
  -DCONFIG_NINLIL_RF_TX_POWER_DBM=-9
  -DCONFIG_NINLIL_RF_SF=9
  -DCONFIG_NINLIL_RF_BW_125=1
  -DCONFIG_NINLIL_RF_CR_DENOMINATOR=5
  -DCONFIG_NINLIL_RF_PREAMBLE_SYMBOLS=8
)
safe_config=(
  "${base_config[@]}"
  '-DCONFIG_NINLIL_RF_REGION=""'
  -DCONFIG_NINLIL_RF_FREQUENCY_HZ=0
)
active_config=(
  "${base_config[@]}"
  -DCONFIG_NINLIL_RF_GATE_POLARITY_CONFIRMED=1
  -DCONFIG_NINLIL_RF_GATE_RX_ACTIVE_HIGH=1
  '-DCONFIG_NINLIL_RF_REGION="HIL"'
  -DCONFIG_NINLIL_RF_FREQUENCY_HZ=920000000
)

for compiler in "${CC:-gcc}" "${CLANG:-clang}"; do
  command -v "$compiler" >/dev/null
  "$compiler" "${common[@]}" "${platform_sources[@]}"

  # Repository-safe defaults: disabled booleans are deliberately undefined,
  # matching ESP-IDF's generated sdkconfig.h behavior.
  "$compiler" "${common[@]}" "${safe_config[@]}" \
    -DCONFIG_NINLIL_M1_MODE_DIAGNOSTIC=1 "$app"

  "$compiler" "${common[@]}" "${active_config[@]}" \
    -DCONFIG_NINLIL_M1_MODE_DIAGNOSTIC=1 \
    -DCONFIG_NINLIL_DIAGNOSTIC_INITIATOR=1 \
    -DCONFIG_NINLIL_RF_TX_ENABLE=1 "$app"

  "$compiler" "${common[@]}" "${active_config[@]}" \
    -DCONFIG_NINLIL_M1_MODE_DELIVERY=1 "$app"

  "$compiler" "${common[@]}" "${active_config[@]}" \
    -DCONFIG_NINLIL_M1_MODE_DELIVERY=1 \
    -DCONFIG_NINLIL_DELIVERY_SUBMIT_ON_BOOT=1 \
    -DCONFIG_NINLIL_RF_TX_ENABLE=1 "$app"
done

real_driver="$root/third_party/sx126x_driver/src"
if [[ -d "$real_driver" ]]; then
  real_common=(
    -std=c11 -fsyntax-only
    -I"$real_driver"
    -I"$root/include"
    -I"$root/tests/esp_stub"
    -I"$root/src"
    -I"$root/ports/flash"
    -I"$root/ports/esp32s3"
    -I"$root/ports/esp32s3/include"
    -DESP_PLATFORM=1
    -Wall -Wextra -Wpedantic -Werror -Wshadow -Wconversion
    -Wsign-conversion -Wformat=2 -Wundef -Wcast-align -Wvla
    -Wstrict-prototypes -Wmissing-prototypes -fno-common
  )
  for compiler in "${CC:-gcc}" "${CLANG:-clang}"; do
    "$compiler" "${real_common[@]}" "${platform_sources[@]}"
    "$compiler" -std=c11 -fsyntax-only -I"$real_driver" \
      "$real_driver/sx126x.c"
  done
  echo "Semtech v2.5.0 header/source API syntax PASS"
fi

echo "Ninlil ESP32-S3 strict syntax PASS"
