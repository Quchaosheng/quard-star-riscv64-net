#!/usr/bin/env bash
set -eu

root=${QS_ROOT:-$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)}
stage=${QS_STAGE:-m2c}
test_name=${QS_TEST_NAME:-m2c-smoke}
out=$root/out/$stage
qemu=${QS_QEMU:-$out/qemu/qemu-system-riscv64}
timeout=${QS_SMOKE_TIMEOUT:-30}
kernel_log=$out/kernel.log
qemu_log=$out/qemu.err
combined=$out/qemu.log

has_success_markers() {
  grep -q 'QS:BOOT_OK' "$combined" 2>/dev/null &&
    grep -q 'QS:KERNEL_READY' "$combined" 2>/dev/null &&
    grep -q 'QS:BLOCK_OK' "$combined" 2>/dev/null &&
    grep -q 'QS:HART_ONLINE:0' "$combined" 2>/dev/null &&
    grep -q 'QS:HART_ONLINE:1' "$combined" 2>/dev/null &&
    grep -q 'QS:SMP_ALLOC_OK' "$combined" 2>/dev/null &&
    grep -q 'QS:SMP_SCHED_OK' "$combined" 2>/dev/null &&
    grep -q 'QS:WAIT_OK' "$combined" 2>/dev/null &&
    grep -q 'QS:SEM_TIMEOUT_OK' "$combined" 2>/dev/null &&
    grep -q 'QS:IPI_OK' "$combined" 2>/dev/null &&
    grep -q 'QS:RFENCE_OK' "$combined" 2>/dev/null &&
    grep -q "QS:TEST_PASS:$test_name" "$combined" 2>/dev/null
}

for file in "$qemu" "$out/fw/fw.bin" "$out/disk/disk.img"; do
  if [ ! -e "$file" ]; then
    echo "error: missing $file; build $stage first" >&2
    exit 1
  fi
done

rm -f "$kernel_log" "$qemu_log" "$combined"
"$qemu" \
  -M quard-star -m 1G -smp 2 -bios none \
  -drive if=pflash,bus=0,unit=0,format=raw,file="$out/fw/fw.bin" \
  -drive file="$out/disk/disk.img",if=none,format=raw,id=x0 \
  -device virtio-blk-device,drive=x0,bus=virtio-mmio-bus.0 \
  -display none -monitor none \
  -serial file:"$kernel_log" \
  2>"$qemu_log" &
pid=$!

i=0
limit=$((timeout * 10))
while [ "$i" -lt "$limit" ]; do
  cat "$kernel_log" 2>/dev/null > "$combined" || true
  if grep -q 'QS:TEST_FAIL' "$combined" 2>/dev/null; then
    break
  fi
  if ! kill -0 "$pid" 2>/dev/null; then
    break
  fi
  sleep 0.1
  i=$((i + 1))
done

if kill -0 "$pid" 2>/dev/null; then
  kill "$pid" 2>/dev/null || true
  wait "$pid" 2>/dev/null || true
  qemu_status=124
else
  set +e
  wait "$pid"
  qemu_status=$?
  set -e
fi
cat "$kernel_log" 2>/dev/null > "$combined" || true

success=0
if [ "$qemu_status" -eq 0 ] && has_success_markers &&
   ! grep -q 'QS:TEST_FAIL' "$combined" 2>/dev/null; then
  success=1
fi

if [ "$test_name" = m2c-stress ]; then
  grep -q 'QS:STRESS_ALLOC_OPS:100000' "$combined" 2>/dev/null || success=0
  grep -q 'QS:STRESS_MIGRATIONS:10000' "$combined" 2>/dev/null || success=0
  elapsed=$(tr -d '\000\r' < "$combined" | \
    sed -n 's/^QS:STRESS_ELAPSED_TICKS:\([0-9][0-9]*\)$/\1/p' | tail -1)
  if [ -z "$elapsed" ] || [ "$elapsed" -lt 1200000000 ]; then
    success=0
  fi
fi

if [ "$success" -ne 1 ]; then
  if [ "$qemu_status" -ne 0 ]; then
    echo "error: QEMU guest exit status $qemu_status" >&2
  fi
  if [ "$test_name" = m2c-stress ]; then
    echo "error: stress counters or minimum duration not satisfied" >&2
  fi
  for marker in QS:BOOT_OK QS:KERNEL_READY QS:BLOCK_OK \
    QS:HART_ONLINE:0 QS:HART_ONLINE:1 QS:SMP_ALLOC_OK QS:SMP_SCHED_OK \
    QS:WAIT_OK QS:SEM_TIMEOUT_OK QS:IPI_OK QS:RFENCE_OK \
    "QS:TEST_PASS:$test_name"
  do
    grep -q "$marker" "$combined" 2>/dev/null || \
      echo "error: missing $marker" >&2
  done
  cat "$combined" >&2
  cat "$qemu_log" >&2
  exit 1
fi

cat "$combined"
echo "PASS: $test_name QEMU smoke"
