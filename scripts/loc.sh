#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
limit=${NINLIL_PROJECT_LOC_LIMIT:-50000}
mapfile -d '' files < <(
  find "$root" -type f \
    \( -name '*.c' -o -name '*.h' -o -name '*.md' -o \
       -name 'CMakeLists.txt' -o -name '*.sh' -o -name '*.toml' -o \
       -name '*.csv' -o -name '*.py' -o -name '*.cmake' -o \
       -name '*.json' -o -name '*.yml' -o -name '*.yaml' \) \
    ! -path "$root/.git/*" \
    ! -path "$root/build*/*" \
    ! -path "$root/.build-*/*" \
    ! -path "$root/.ci-build/*" \
    ! -path "$root/.ci-build-*/*" \
    ! -path "$root/.baseline-build/*" \
    ! -path "$root/.qa-*/*" \
    ! -path "$root/.verify-build/*" \
    ! -path "$root/.verify-*/*" \
    ! -path "$root/.fake-build/*" \
    ! -path "$root/third_party/sx126x_driver/src/*" \
    ! -path "$root/third_party/libedhoc/*" \
    ! -path "$root/third_party/adapted/edhoc_cipher_suite_2.c" \
    ! -path "$root/third_party/adapted/zcbor_encode.c" \
    -print0 | sort -z
)
nonblank=0
physical=0
for file in "${files[@]}"; do
  physical=$((physical + $(wc -l < "$file")))
  nonblank=$((nonblank + $(awk 'NF { n++ } END { print n + 0 }' "$file")))
done
printf 'Project first-party physical lines: %d\n' "$physical"
printf 'Project first-party nonblank lines: %d / %d\n' "$nonblank" "$limit"
if ((nonblank > limit)); then
  echo "Project line budget exceeded" >&2
  exit 1
fi
