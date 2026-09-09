#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
limit=${NINLIL_SECURE_NETWORK_LOC_LIMIT:-12000}
echo 'Secure protocol/storage libraries (autonomous execution owner counted separately)'
union="$root/scripts/secure_network_files.txt"
owner="$root/scripts/autonomous_node_files.txt"
extension="$root/scripts/storage_bulk_radio_files.txt"
field="$root/scripts/field_deployment_files.txt"
lifecycle="$root/scripts/deployment_lifecycle_files.txt"
sorted_paths() { sed 's/\r$//' "$@" | sort; }
if [[ -n $(comm -13 <(sorted_paths "$union") <(sorted_paths "$lifecycle")) ]] ||
   [[ -n $(comm -12 <(sorted_paths "$owner" "$extension" "$field") <(sorted_paths "$lifecycle")) ]]; then
  echo 'Deployment lifecycle must be a disjoint subset of the complete union' >&2
  exit 1
fi
if [[ -n $(comm -13 <(sorted_paths "$union") <(sorted_paths "$field")) ]] ||
   [[ -n $(comm -12 <(sorted_paths "$owner" "$extension") <(sorted_paths "$field")) ]]; then
  echo 'Field deployment ledger must be a disjoint subset of the complete union' >&2
  exit 1
fi
if [[ -n $(comm -13 <(sorted_paths "$union") <(sorted_paths "$extension")) ]] ||
   [[ -n $(comm -12 <(sorted_paths "$owner") <(sorted_paths "$extension")) ]]; then
  echo 'Extension ledger must be a disjoint subset of the complete union' >&2
  exit 1
fi
if [[ -n $(comm -13 <(sorted_paths "$union") <(sorted_paths "$owner")) ]]; then
  echo 'Autonomous ledger contains a path outside the complete secure union' >&2
  exit 1
fi
comm -23 <(sorted_paths "$union") <(sorted_paths "$owner" "$extension" "$field" "$lifecycle") | python3 "$root/scripts/count_sources.py" "$limit"
echo 'Autonomous owner and reference integration'
python3 "$root/scripts/count_sources.py" 5500 < "$owner"
echo 'Storage collection, bulk profile and TX power adaptation'
python3 "$root/scripts/count_sources.py" 4000 < "$extension"
echo 'Dynamic enrollment, discovery and deployment settings'
python3 "$root/scripts/count_sources.py" 3000 < "$field"
echo 'Root replacement, battery suspension and explicit deployment retirement'
python3 "$root/scripts/count_sources.py" 1800 < "$lifecycle"
echo 'Historical evidence (also included in project-wide first-party accounting)'
python3 "$root/scripts/count_sources.py" 50000 < "$root/scripts/secure_network_historical_files.txt"
