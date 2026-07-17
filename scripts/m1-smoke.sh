#!/usr/bin/env bash
set -eu

root=${QS_ROOT:-$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)}
out=$root/out/m1
qemu=${QS_QEMU:-$out/qemu/qemu-system-riscv64}
timeout=${QS_SMOKE_TIMEOUT:-20}
kernel_log=$out/kernel.log
qemu_log=$out/qemu.err
combined=$out/qemu.log

has_success_markers() {
  grep -q 'QS:BOOT_OK' "$combined" 2>/dev/null &&
    grep -q 'QS:KERNEL_READY' "$combined" 2>/dev/null &&
    grep -q 'QS:BLOCK_OK' "$combined" 2>/dev/null &&
    grep -q 'QS:TEST_PASS:m1-smoke' "$combined" 2>/dev/null
}

for file in "$qemu" "$out/fw/fw.bin" "$out/disk/disk.img"; do
  if [ ! -e "$file" ]; then
    echo "error: missing $file; run make m1-build" >&2
    exit 1
  fi
done

rm -f "$kernel_log" "$qemu_log" "$combined"
"$qemu" \
  -M quard-star -m 1G -smp 1 -bios none \
  -drive if=pflash,bus=0,unit=0,format=raw,file="$out/fw/fw.bin" \
  -drive file="$out/disk/disk.img",if=none,format=raw,id=x0 \
  -device virtio-blk-device,drive=x0,bus=virtio-mmio-bus.0 \
  -display none -monitor none \
  -serial file:"$kernel_log" \
  2>"$qemu_log" &
pid=$!

success=0
i=0
limit=$((timeout * 10))
while [ "$i" -lt "$limit" ]; do
  cat "$kernel_log" 2>/dev/null > "$combined" || true
  if grep -q 'QS:TEST_FAIL' "$combined" 2>/dev/null; then
    break
  fi
  if has_success_markers; then
    success=1
    break
  fi
  if ! kill -0 "$pid" 2>/dev/null; then
    break
  fi
  sleep 0.1
  i=$((i + 1))
done

kill "$pid" 2>/dev/null || true
wait "$pid" 2>/dev/null || true
cat "$kernel_log" 2>/dev/null > "$combined" || true

if grep -q 'QS:TEST_FAIL' "$combined" 2>/dev/null; then
  success=0
elif has_success_markers; then
  success=1
fi

if [ "$success" -ne 1 ]; then
  for marker in QS:BOOT_OK QS:KERNEL_READY QS:BLOCK_OK QS:TEST_PASS:m1-smoke; do
    grep -q "$marker" "$combined" 2>/dev/null || echo "error: missing $marker" >&2
  done
  cat "$qemu_log" >&2
  exit 1
fi

cat "$combined"
echo "PASS: M1 QEMU smoke"
