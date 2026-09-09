#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
limit=${NINLIL_PROJECT_LOC_LIMIT:-50000}
mapfile -d '' files < <(
  find "$root" -type f \
    \( -name '*.c' -o -name '*.h' -o -name '*.md' -o \
       -name 'CMakeLists.txt' -o -name 'requirements.txt' -o -name '*.sh' -o -name '*.toml' -o \
       -name '*.csv' -o -name '*.py' -o -name '*.cmake' -o -name '*.cmake.in' -o \
       -name '*.json' -o -name '*.jsonl' -o -name '*.yml' -o -name '*.yaml' \) \
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
    -print0 | sort -z
)
echo 'Project first-party source (including historical evidence)'
printf '%s\n' "${files[@]}" | python3 "$root/scripts/count_sources.py" "$limit"
