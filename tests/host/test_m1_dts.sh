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
  platform/quard-star/dts/quard_star_kernel.dts \
  platform/quard-star/dts/quard_star_kernel_m2.dts
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
  case "$name" in
    quard_star_kernel*)
      dtc -I dtb -O dts -o "$tmp/$name.roundtrip.dts" \
        "$tmp/$name.dtb" 2>/dev/null
      count=$(grep -Fc 'compatible = "virtio,mmio";' \
        "$tmp/$name.roundtrip.dts" || true)
      [ "$count" -eq 2 ] || \
        fail "$dts must expose exactly two VirtIO MMIO transports"
      ;;
  esac
done

echo "PASS: M1 DTS behavior"
