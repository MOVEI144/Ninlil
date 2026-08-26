#!/usr/bin/env bash
set -euo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
source_dir="$root/tests/m1"
build_root=${NINLIL_M1_BUILD_ROOT:-"$root/.ci-build-m1"}
jobs=${NINLIL_JOBS:-2}
cmake_bin=${CMAKE:-cmake}
ctest_bin=${CTEST:-ctest}
gcc_bin=${GCC:-gcc}
clang_bin=${CLANG:-clang}
ninja_bin=${NINJA:-ninja}
clang_format_bin=${CLANG_FORMAT:-clang-format}

resolve_tool() {
  local value=$1

  if [[ "$value" == */* ]]; then
    [[ -x "$value" ]] || {
      echo "required executable not found: $value" >&2
      exit 1
    }
    printf '%s\n' "$value"
    return
  fi
  command -v "$value" || {
    echo "required executable not found in PATH: $value" >&2
    exit 1
  }
}

cmake_bin=$(resolve_tool "$cmake_bin")
ctest_bin=$(resolve_tool "$ctest_bin")
gcc_bin=$(resolve_tool "$gcc_bin")
clang_bin=$(resolve_tool "$clang_bin")
ninja_bin=$(resolve_tool "$ninja_bin")
clang_format_bin=$(resolve_tool "$clang_format_bin")

run_build() {
  local name=$1
  local compiler=$2
  local sanitize=$3
  local build="$build_root/$name"

  rm -rf "$build"
  "$cmake_bin" -S "$source_dir" -B "$build" -G Ninja \
    -DCMAKE_MAKE_PROGRAM="$ninja_bin" \
    -DCMAKE_C_COMPILER="$compiler" \
    -DCMAKE_BUILD_TYPE=Debug \
    -DNINLIL_SANITIZE="$sanitize"
  "$cmake_bin" --build "$build" --parallel "$jobs"
  if [[ "$sanitize" == ON ]]; then
    ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
    UBSAN_OPTIONS=halt_on_error=1 \
      "$ctest_bin" --test-dir "$build" --output-on-failure
  else
    "$ctest_bin" --test-dir "$build" --output-on-failure
  fi
}

mkdir -p "$build_root"
run_build gcc "$gcc_bin" OFF
run_build clang "$clang_bin" OFF
run_build gcc-sanitize "$gcc_bin" ON
run_build clang-sanitize "$clang_bin" ON

format_roots=(
  "$root/include"
  "$root/src"
  "$root/ports/flash"
  "$root/ports/esp32s3"
  "$root/embedded/esp32s3"
  "$root/tests/esp_stub"
)
mapfile -d '' format_files < <(
  find "${format_roots[@]}" -type f \( -name '*.c' -o -name '*.h' \) \
    ! -path "$root/third_party/*" -print0 | sort -z
)
format_files+=(
  "$root/tests/test_core.c"
  "$root/tests/test_diag_radio.c"
  "$root/tests/test_flash.c"
  "$root/tests/test_radio_delivery.c"
  "$root/tests/test_support.c"
  "$root/tests/test_support.h"
  "$root/tests/test_sx1262_hal.c"
  "$root/tests/test_sx1262_physical.c"
)
"$clang_format_bin" --dry-run --Werror "${format_files[@]}"

"$root/scripts/check_sx126x_driver.sh"
CC="$gcc_bin" CLANG="$clang_bin" "$root/scripts/check_esp_syntax.sh"
"$root/scripts/static_analysis.sh" "$gcc_bin" "$clang_bin"
"$root/scripts/loc_m1_software.sh"

if git -C "$root" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
  git -C "$root" diff --check
fi
for script in "$root"/scripts/*.sh; do
  bash -n "$script"
done

echo "Ninlil M1 software CI PASS"
