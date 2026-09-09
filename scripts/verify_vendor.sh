#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
build=${1:?pass an out-of-tree vendor verification directory}
cmake -S "$root/tests/vendor" -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build "$build" --parallel "${NINLIL_JOBS:-2}"
"$build/vendor/tests/libedhoc_module_tests"
