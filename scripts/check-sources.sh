#!/usr/bin/env bash
set -eu

root=${QS_ROOT:-$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)}
cd "$root"

for name in qemu opensbi freertos dtc nanoprintf; do
  submodule=third_party/$name
  expected=$(git ls-files -s "$submodule" | awk '{print $2}')
  if [ -z "$expected" ]; then
    echo "error: missing or untracked $submodule" >&2
    exit 1
  fi
  if [ ! -e "$submodule/.git" ]; then
    echo "error: $submodule is at uninitialized, expected $expected" >&2
    exit 1
  fi
  actual=$(git -C "$submodule" rev-parse HEAD 2>/dev/null || true)
  if [ "$expected" != "$actual" ]; then
    echo "error: $submodule is at ${actual:-uninitialized}, expected $expected" >&2
    exit 1
  fi
done

test -f third_party/qemu/COPYING
test -f third_party/opensbi/COPYING.BSD
test -f third_party/freertos/LICENSE.md
test -f third_party/dtc/GPL
test -f third_party/nanoprintf/LICENSE

for series in patches/*/series; do
  [ -e "$series" ] || continue
  name=$(basename "${series%/series}")
  while IFS= read -r patch; do
    [ -n "$patch" ] || continue
    case "$patch" in \#*) continue ;; esac
    git -C "third_party/$name" apply --check "$root/patches/$name/$patch"
  done < "$series"
done

if [ -f out/downloads/ff15.zip ]; then
  QS_ROOT="$root" "$root/scripts/fetch-fatfs.sh" --check
fi
