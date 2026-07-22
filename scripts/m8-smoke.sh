#!/usr/bin/env bash
set -eu
root=${QS_ROOT:-$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)}
out=$root/out/m8
qemu=$out/qemu/qemu-system-riscv64
log=$out/kernel.log
trusted_log=$out/trusted.log
err=$out/qemu.err
rm -f "$log" "$trusted_log" "$err"
"$qemu" -M quard-star -m 1G -smp 8 -bios none \
  -drive if=pflash,bus=0,unit=0,format=raw,file="$out/fw/fw.bin" \
  -drive file="$out/disk/disk.img",if=none,format=raw,id=x0 \
  -device virtio-blk-device,drive=x0,bus=virtio-mmio-bus.0 \
  -display none -monitor none \
  -serial file:"$log" -serial null -serial file:"$trusted_log" 2>"$err" &
pid=$!
trap 'kill "$pid" 2>/dev/null || true; wait "$pid" 2>/dev/null || true' EXIT
for _ in $(seq 1 300); do
  if grep -q 'QS:TEST_PASS:m8-smoke' "$log" 2>/dev/null; then break; fi
  sleep 0.1
done
if ! grep -q 'QS:TEST_PASS:m8-smoke' "$log" 2>/dev/null; then
  cat "$log" >&2; cat "$err" >&2; exit 1
fi
if ! grep -q 'QS:TRUSTED_READY' "$trusted_log" 2>/dev/null; then
  echo 'error: trusted firmware did not report readiness' >&2
  cat "$trusted_log" >&2
  exit 1
fi
for hart in 0 1 2 3 4 5 6; do
  grep -q "QS:HART_ONLINE:$hart" "$log"
done
if grep -q 'QS:HART_ONLINE:7' "$log" 2>/dev/null; then
  echo 'error: trusted hart7 entered the ordinary kernel' >&2
  exit 1
fi
cat "$log"
echo 'PASS: m8 seven ordinary harts with trusted hart7'
