#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
commit=2b245e4a8aedb675ded18d085801e20a980e8e5c
target="$root/third_party/libedhoc"
if [[ ! -d "$target/.git" ]]; then
  [[ ! -e "$target" ]] || { echo 'libedhoc target already exists' >&2; exit 1; }
  git clone --no-checkout https://github.com/kamil-kielbasa/libedhoc.git "$target"
  git -C "$target" checkout --detach "$commit"
fi
test "$(git -C "$target" rev-parse HEAD)" = "$commit"
test -z "$(git -C "$target" status --porcelain --untracked-files=no)"
git -C "$target" submodule update --init --recursive --depth 1
git -C "$target" submodule status --recursive
echo 'Pinned libedhoc dependencies ready; not a security acceptance result'
