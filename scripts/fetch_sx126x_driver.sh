#!/usr/bin/env bash
set -euo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
destination="$root/third_party/sx126x_driver"
repository=https://github.com/Lora-net/sx126x_driver.git
commit=a10c5dfdf89788c6ac805e9fe98889de44175aa2

if [[ -e "$destination" ]]; then
  echo "destination already exists: $destination" >&2
  exit 1
fi

temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT

git clone --filter=blob:none --no-checkout "$repository" "$temporary/repo"
git -C "$temporary/repo" checkout --detach "$commit"
actual=$(git -C "$temporary/repo" rev-parse HEAD)
if [[ "$actual" != "$commit" ]]; then
  echo "unexpected sx126x_driver commit: $actual" >&2
  exit 1
fi

mkdir -p "$destination/src"
for file in sx126x.c sx126x.h sx126x_hal.h sx126x_regs.h sx126x_status.h; do
  install -m 0644 "$temporary/repo/src/$file" "$destination/src/$file"
done
install -m 0644 "$temporary/repo/LICENSE.txt" "$destination/LICENSE.txt"
cat > "$destination/PROVENANCE" <<PROVENANCE
repository=$repository
version=v2.5.0
commit=$commit
fetched_utc=$(date -u +%Y-%m-%dT%H:%M:%SZ)
files=src/sx126x.c,src/sx126x.h,src/sx126x_hal.h,src/sx126x_regs.h,src/sx126x_status.h,LICENSE.txt
PROVENANCE

(
  cd "$destination"
  sha256sum LICENSE.txt PROVENANCE src/* > SHA256SUMS
)
echo "installed Semtech sx126x_driver $commit into $destination"
