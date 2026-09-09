#!/usr/bin/env bash
# Reproduce the fixed baseline in a complete checkout; never a full CI claim.
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$root"
command -v git >/dev/null
while read -r expected path; do
    actual="$(git hash-object "$path")"
    if [[ "$actual" != "$expected" ]]; then
        printf 'Source changed: %s; this witness describes a950f7c, not the new policy.\n' "$path" >&2
        exit 2
    fi
done <<'HASHES'
1f25fbae67edae9d92c96048223af4c0a787bff4 src/ninlil_airtime.c
fc6f5f0a035bce7ef8c779a48dd5a8d70df997b3 src/ninlil_radio_adapt.c
4c87e0c0c52c7106f8eea53ed35ac3842a0e3001 include/ninlil_airtime.h
2f1691a2c4182b5493d50a3301dc77ed494bc9bb include/ninlil_radio_adapt.h
HASHES
build="$(mktemp -d "${TMPDIR:-/tmp}/ninlil-witness.XXXXXXXX")"
trap 'rm -rf "$build"' EXIT
for cc in "${GCC:-gcc}" "${CLANG:-clang}"; do
    command -v "$cc" >/dev/null
    "$cc" --version | sed -n '1p'
    for mode in normal sanitize; do
        flags=()
        if [[ "$mode" == sanitize ]]; then
            flags=(-fsanitize=address,undefined -fno-omit-frame-pointer)
        fi
        "$cc" -std=c11 -Wall -Wextra -Werror -pedantic -g "${flags[@]}" \
            -Iinclude src/ninlil_airtime.c src/ninlil_radio_adapt.c \
            reviews/2026-09-09-adaptive/witness.c -o "$build/witness"
        printf 'Mode: %s\n' "$mode"
        ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
            UBSAN_OPTIONS=halt_on_error=1 "$build/witness"
    done
done
