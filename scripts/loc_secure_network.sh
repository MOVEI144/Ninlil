#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
limit=${NINLIL_SECURE_NETWORK_LOC_LIMIT:-12000}
physical=0
nonblank=0
while IFS= read -r relative; do
  file="$root/$relative"
  [[ -f "$file" ]] || { echo "missing secure-network source: $relative" >&2; exit 1; }
  physical=$((physical + $(wc -l < "$file")))
  nonblank=$((nonblank + $(awk 'NF { n++ } END { print n + 0 }' "$file")))
done < "$root/scripts/secure_network_files.txt"
printf 'Secure network physical lines: %d\n' "$physical"
printf 'Secure network nonblank lines: %d / %d\n' "$nonblank" "$limit"
((nonblank <= limit)) || { echo 'Secure network line budget exceeded' >&2; exit 1; }
