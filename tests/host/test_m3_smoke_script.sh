#!/usr/bin/env bash
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

mkdir -p "$tmp/out/m3/qemu" "$tmp/out/m3/fw" "$tmp/out/m3/disk"
: > "$tmp/out/m3/fw/fw.bin"
: > "$tmp/out/m3/disk/disk.img"

cat > "$tmp/fake-qemu" <<'EOF'
#!/usr/bin/env bash
set -eu

kernel_log=
for arg in "$@"; do
  case "$arg" in
    file:*) kernel_log=${arg#file:} ;;
  esac
done
[ -n "$kernel_log" ]

cat > "$kernel_log" <<'LOG'
QS:BOOT_OK
QS:KERNEL_READY
QS:BLOCK_OK
QS:HART_ONLINE:0
QS:HART_ONLINE:1
QS:SMP_ALLOC_OK
QS:SMP_SCHED_OK
QS:WAIT_OK
QS:SEM_TIMEOUT_OK
QS:IPI_OK
QS:RFENCE_OK
QS:VIRTQUEUE_OK
QS:BLOCK_IRQ_OK
QS:BLOCK_STRESS_OK
QS:FATFS_OK
QS:TEST_PASS:m3-smoke
LOG

if [ "${QS_FAKE_MISSING:-0}" -eq 1 ]; then
  sed -i '/QS:FATFS_OK/d' "$kernel_log"
fi
exit "${QS_FAKE_EXIT:-0}"
EOF
chmod +x "$tmp/fake-qemu"

run_m3() {
  QS_ROOT="$tmp" QS_QEMU="$tmp/fake-qemu" QS_SMOKE_TIMEOUT=2 \
    "$root/scripts/m3-smoke.sh"
}

run_m3 >"$tmp/good.out" 2>"$tmp/good.err" || \
  fail "complete M3 markers and guest exit zero should pass"

if QS_FAKE_MISSING=1 run_m3 >"$tmp/missing.out" 2>"$tmp/missing.err"; then
  fail "missing M3 marker must fail"
fi
grep -Fq 'QS:FATFS_OK' "$tmp/missing.err" || \
  fail "missing marker failure should name QS:FATFS_OK"

if QS_FAKE_EXIT=7 run_m3 >"$tmp/exit.out" 2>"$tmp/exit.err"; then
  fail "nonzero guest exit must fail"
fi
grep -qi 'exit' "$tmp/exit.err" || \
  fail "nonzero guest exit failure should be explicit"

echo "PASS: M3 smoke script behavior"
