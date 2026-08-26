#!/usr/bin/env bash
set -euo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
driver="$root/third_party/sx126x_driver"
expected_repository=https://github.com/Lora-net/sx126x_driver.git
expected_commit=a10c5dfdf89788c6ac805e9fe98889de44175aa2
required=(
  LICENSE.txt
  PROVENANCE
  SHA256SUMS
  src/sx126x.c
  src/sx126x.h
  src/sx126x_hal.h
  src/sx126x_regs.h
  src/sx126x_status.h
)

require_text() {
  local file=$1
  local text=$2

  grep -Fqx "$text" "$file" || {
    echo "missing pinned provenance in $file: $text" >&2
    exit 1
  }
}

require_text "$root/third_party/UPSTREAM.toml" \
  "repository = \"$expected_repository\""
require_text "$root/third_party/UPSTREAM.toml" \
  "commit = \"$expected_commit\""
grep -Fq "commit=$expected_commit" "$root/scripts/fetch_sx126x_driver.sh" || {
  echo "fetch script does not pin the required sx126x commit" >&2
  exit 1
}

if [[ ! -d "$driver" ]]; then
  echo "Semtech driver not vendored; exact fetch is deferred to ESP build"
  exit 0
fi

for file in "${required[@]}"; do
  [[ -f "$driver/$file" ]] || {
    echo "missing required Semtech file: $file" >&2
    exit 1
  }
done
require_text "$driver/PROVENANCE" "repository=$expected_repository"
require_text "$driver/PROVENANCE" "version=v2.5.0"
require_text "$driver/PROVENANCE" "commit=$expected_commit"

if find "$driver/src" -maxdepth 1 -type f \
  \( -iname '*lr_fhss*' -o -iname '*bpsk*' \) -print -quit | grep -q .; then
  echo "LR-FHSS/BPSK files are outside the M1 dependency subset" >&2
  exit 1
fi
(
  cd "$driver"
  sha256sum -c SHA256SUMS
)

echo "Semtech sx126x_driver provenance PASS"
