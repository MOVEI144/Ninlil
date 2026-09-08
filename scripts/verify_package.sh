#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
build=${1:?Usage: verify_package.sh BUILT_SECURE_HOST_DIRECTORY}
temp=$(mktemp -d)
trap 'rm -rf "$temp"' EXIT
cmake --install "$build" --prefix "$temp/install" >/dev/null
mv "$temp/install" "$temp/relocated"
for backend in posix flash_runtime; do
  cmake -S "$root/tests/consumer" -B "$temp/consumer-$backend" -G Ninja \
    -DCMAKE_PREFIX_PATH="$temp/relocated" -DNinlil_JOURNAL_BACKEND="$backend"
  cmake --build "$temp/consumer-$backend"
  "$temp/consumer-$backend/consumer"
done
reject() {
  local name=$1 expected=$2
  shift 2
  if cmake -S "$root/tests/consumer" -B "$temp/$name" -G Ninja \
      -DCMAKE_PREFIX_PATH="$temp/relocated" "$@" >"$temp/$name.log" 2>&1; then
    echo "Package unexpectedly accepted $name" >&2
    exit 1
  fi
  grep -Fq "$expected" "$temp/$name.log"
  echo "Rejected $name with expected package diagnostic"
}
reject conflicting-backends 'One Ninlil journal backend is required per build' \
  -DNinlil_JOURNAL_BACKEND=posix -DCONSUMER_SWITCH_BACKEND=flash_runtime
reject invalid-backend 'Select posix or flash_runtime' -DNinlil_JOURNAL_BACKEND=unknown
if grep -F -e "$root/" -e "$build/" "$temp/relocated/lib/cmake/Ninlil/"*.cmake; then
  echo 'Installed package leaked a source/build dependency path' >&2
  exit 1
fi
cmake -S "$root" -B "$temp/core" -G Ninja \
  -DNINLIL_BUILD_SECURE=OFF -DNINLIL_BUILD_TESTS=OFF
cmake --build "$temp/core" --parallel "${NINLIL_JOBS:-2}"
cmake --install "$temp/core" --prefix "$temp/core-install" >/dev/null
cmake -S "$root/tests/consumer" -B "$temp/core-consumer" -G Ninja \
  -DCMAKE_PREFIX_PATH="$temp/core-install" -DCONSUMER_SECURE=OFF
cmake --build "$temp/core-consumer"
"$temp/core-consumer/consumer"
echo 'Relocated secure POSIX/NOR and Core-only package consumers PASS'
