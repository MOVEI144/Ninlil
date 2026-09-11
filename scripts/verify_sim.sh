#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
binary=${1:?usage: verify_sim.sh /path/to/ninlil_sim}
temp=$(mktemp -d)
trap 'rm -rf "$temp"' EXIT
for scenario in direct star recovery; do
  manifest="$root/tests/sim/manifests/$scenario.txt"
  "$binary" "$manifest" > "$temp/$scenario.first"
  "$binary" "$manifest" > "$temp/$scenario.second"
  cmp "$temp/$scenario.first" "$temp/$scenario.second"
  sha256sum "$manifest" "$temp/$scenario.first"
done
sed 's/receipt_hold_ms=0/receipt_hold_ms=120000/' \
  "$root/tests/sim/manifests/star.txt" > "$temp/receipt-hold.txt"
set +e
"$binary" "$temp/receipt-hold.txt" > "$temp/receipt-hold.report"
result=$?
set -e
[[ "$result" == 2 ]]
grep -q 'satisfied=0 active=32 remote_offered=32 on_time=0 not_on_time=32' \
  "$temp/receipt-hold.report"
grep -q 'ACTIVE,1,0,[0-9]*,NA,NA,120000000,' "$temp/receipt-hold.report"
for invalid in duplicate missing overflow unknown; do
  cp "$root/tests/sim/manifests/star.txt" "$temp/$invalid.txt"
  case "$invalid" in
    duplicate) echo 'nodes=5' >> "$temp/$invalid.txt" ;;
    missing) sed -i '/^seed=/d' "$temp/$invalid.txt" ;;
    overflow) sed -i 's/seed=7/seed=4294967296/' "$temp/$invalid.txt" ;;
    unknown) echo 'cloud=1' >> "$temp/$invalid.txt" ;;
  esac
  set +e
  "$binary" "$temp/$invalid.txt" > "$temp/invalid.out" 2> "$temp/invalid.err"
  result=$?
  set -e
  [[ "$result" == 1 && ! -s "$temp/invalid.out" ]]
  grep -q 'invalid manifest (no runtime opened)' "$temp/invalid.err"
done
echo 'W02 reproducibility, explicit incomplete exit, malformed CLI input PASS'
