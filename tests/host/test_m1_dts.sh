#!/usr/bin/env bash
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

for dts in \
  platform/quard-star/dts/quard_star_sbi.dts \
  platform/quard-star/dts/quard_star_kernel.dts
do
  name=$(basename "$dts" .dts)
  if ! dtc -I dts -O dtb -o "$tmp/$name.dtb" "$root/$dts" \
    >"$tmp/$name.out" 2>"$tmp/$name.err"; then
    cat "$tmp/$name.err" >&2
    fail "dtc could not compile $dts"
  fi
  if [ -s "$tmp/$name.err" ]; then
    cat "$tmp/$name.err" >&2
    fail "dtc emitted diagnostics for $dts"
  fi
done

echo "PASS: M1 DTS behavior"
