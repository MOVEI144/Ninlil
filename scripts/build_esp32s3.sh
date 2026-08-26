#!/usr/bin/env bash
set -euo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
project="$root/embedded/esp32s3"

: "${IDF_PATH:?Source the pinned ESP-IDF export.sh first}"
command -v idf.py >/dev/null 2>&1 || { echo "idf.py is not available; source $IDF_PATH/export.sh" >&2; exit 1; }
[[ "$(idf.py --version)" == *"v6.0.2"* ]] || { echo "Ninlil M1 requires ESP-IDF v6.0.2" >&2; exit 1; }
[[ -f "$root/third_party/sx126x_driver/src/sx126x.c" ]] || "$root/scripts/fetch_sx126x_driver.sh"
"$root/scripts/check_sx126x_driver.sh"
idf.py -C "$project" set-target esp32s3
idf.py -C "$project" build
