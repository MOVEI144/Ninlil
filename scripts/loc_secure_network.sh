#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
limit=${NINLIL_SECURE_NETWORK_LOC_LIMIT:-12000}
echo 'Secure protocol/storage libraries (autonomous execution owner counted separately)'
union="$root/scripts/secure_network_files.txt"
owner="$root/scripts/autonomous_node_files.txt"
if [[ -n $(comm -13 <(sort "$union") <(sort "$owner")) ]]; then
  echo 'Autonomous ledger contains a path outside the complete secure union' >&2
  exit 1
fi
comm -23 <(sort "$union") <(sort "$owner") | python3 "$root/scripts/count_sources.py" "$limit"
echo 'Autonomous owner and reference integration'
python3 "$root/scripts/count_sources.py" 5500 < "$owner"
echo 'Historical evidence (also included in project-wide first-party accounting)'
python3 "$root/scripts/count_sources.py" 50000 < "$root/scripts/secure_network_historical_files.txt"
