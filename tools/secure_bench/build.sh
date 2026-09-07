#!/usr/bin/env bash
# Run inside the pinned ESP-IDF v6.0.2 environment; no flashing or downloads.
set -euo pipefail
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
node=${1:?node 1, 2 or 3}
output=${2:?new absolute artifact directory}
[[ "$node" == 1 || "$node" == 2 || "$node" == 3 ]]
peer=1
table=partitions.csv
[[ "$node" != 1 ]] || peer=2
[[ "$node" != 3 ]] || table=partitions-preserve-ninlil-st.csv
[[ "$output" == /* && ! -e "$output" ]]
[[ $(idf.py --version) == *v6.0.2* ]]
build=${NINLIL_BENCH_BUILD_ROOT:-"/tmp/ninlil-secure-bench-$node"}
mkdir -p "$output"
grep -v CONFIG_PARTITION_TABLE_CUSTOM_FILENAME "$root/embedded/esp32s3/sdkconfig.defaults" > "$output/sdkconfig"
cat >> "$output/sdkconfig" <<CONFIG
CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="$table"
CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y
CONFIG_NINLIL_M1_MODE_SECURE_BENCH=y
CONFIG_NINLIL_NODE_ID=$node
CONFIG_NINLIL_PEER_ID=$peer
CONFIG_NINLIL_RADIO_INIT_CYCLES=1
CONFIG_NINLIL_RF_TX_ENABLE=y
CONFIG_NINLIL_RF_GATE_POLARITY_CONFIRMED=y
CONFIG_NINLIL_RF_GATE_RX_ACTIVE_HIGH=y
CONFIG_NINLIL_RF_REGION="JP"
CONFIG_NINLIL_RF_FREQUENCY_HZ=921400000
CONFIG_NINLIL_RF_TX_POWER_DBM=-9
CONFIG_NINLIL_RF_SF=7
CONFIG_NINLIL_RF_BW_125=y
CONFIG_NINLIL_RF_CR_DENOMINATOR=5
CONFIG_NINLIL_RF_PREAMBLE_SYMBOLS=8
CONFIG
# Explicit reconfigure is required: copied sdkconfig timestamps may be older
# than generated headers. Never infer the compiled node from sdkconfig alone.
idf.py -C "$root/embedded/esp32s3" -B "$build" \
  -D SDKCONFIG="$output/sdkconfig" -D PROJECT_VER="${NINLIL_BENCH_VERSION:-secure-bench-20260908}" \
  reconfigure build
grep -Fx "#define CONFIG_NINLIL_NODE_ID $node" "$build/config/sdkconfig.h"
cp "$build/config/sdkconfig.h" "$output/"
cp "$build/ninlil_m1.bin" "$output/app.bin"
cp "$build/ninlil_m1.elf" "$output/app.elf"
cp "$build/bootloader/bootloader.bin" "$output/"
cp "$build/partition_table/partition-table.bin" "$output/partitions.bin"
python3 - "$output" <<'PY'
import hashlib, json, sys
from pathlib import Path
p = Path(sys.argv[1])
(p / 'hashes.json').write_text(json.dumps({f.name: hashlib.sha256(f.read_bytes()).hexdigest()
    for f in p.iterdir() if f.is_file() and f.name != 'hashes.json'}, indent=2))
PY
