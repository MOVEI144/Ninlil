#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
limit=${NINLIL_SECURE_NETWORK_LOC_LIMIT:-12000}
echo 'Secure protocol/storage libraries (autonomous execution owner counted separately)'
union="$root/scripts/secure_network_files.txt"
owner="$root/scripts/autonomous_node_files.txt"
extension="$root/scripts/storage_bulk_radio_files.txt"
if [[ -n $(comm -13 <(sort "$union") <(sort "$extension")) ]] ||
   [[ -n $(comm -12 <(sort "$owner") <(sort "$extension")) ]]; then
  echo 'Extension ledger must be a disjoint subset of the complete union' >&2
  exit 1
fi
if [[ -n $(comm -13 <(sort "$union") <(sort "$owner")) ]]; then
  echo 'Autonomous ledger contains a path outside the complete secure union' >&2
  exit 1
fi
comm -23 <(sort "$union") <(cat "$owner" "$extension" | sort) | python3 "$root/scripts/count_sources.py" "$limit"
echo 'Autonomous owner and reference integration'
python3 "$root/scripts/count_sources.py" 5500 < "$owner"
echo 'Storage collection, bulk profile and TX power adaptation'
python3 "$root/scripts/count_sources.py" 4000 < "$extension"
echo 'Historical evidence (also included in project-wide first-party accounting)'
python3 "$root/scripts/count_sources.py" 50000 < "$root/scripts/secure_network_historical_files.txt"
