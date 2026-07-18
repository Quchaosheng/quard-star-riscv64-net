#!/usr/bin/env bash
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

mkdir -p "$tmp/out/m2c/qemu" "$tmp/out/m2c/fw" "$tmp/out/m2c/disk"
: > "$tmp/out/m2c/fw/fw.bin"
: > "$tmp/out/m2c/disk/disk.img"
mkdir -p "$tmp/out/m2c-stress/qemu" "$tmp/out/m2c-stress/fw" \
  "$tmp/out/m2c-stress/disk"
: > "$tmp/out/m2c-stress/fw/fw.bin"
: > "$tmp/out/m2c-stress/disk/disk.img"

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
printf '%s\n' "$@" > "$QS_QEMU_ARGS"

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
LOG

if [ "${QS_FAKE_STRESS:-0}" -eq 1 ]; then
  printf '\000>> \000QS:STRESS_ALLOC_OPS:100000\r\n' >> "$kernel_log"
  printf 'QS:STRESS_MIGRATIONS:10000\r\n' >> "$kernel_log"
  printf 'QS:STRESS_ELAPSED_TICKS:1200000001\r\n' >> "$kernel_log"
  printf 'QS:TEST_PASS:m2c-stress\r\n' >> "$kernel_log"
else
  if [ "${QS_FAKE_BAD_ITERATION:-0}" -eq 1 ]; then
    printf 'QS:FATFS_ITERATIONS:1280\n' >> "$kernel_log"
  fi
  printf 'QS:TEST_PASS:m2c-smoke\n' >> "$kernel_log"
fi

exit "${QS_FAKE_EXIT:-0}"
EOF
chmod +x "$tmp/fake-qemu"

if ! QS_ROOT="$tmp" QS_QEMU="$tmp/fake-qemu" QS_QEMU_ARGS="$tmp/qemu.args" \
QS_SMOKE_TIMEOUT=2 "$root/scripts/m2c-smoke.sh" >"$tmp/good.out" 2>"$tmp/good.err"; then
  cat "$tmp/good.out" >&2
  cat "$tmp/good.err" >&2
  fail "complete M2C marker set and guest exit zero should pass"
fi

paste -sd ' ' "$tmp/qemu.args" | grep -Fq -- '-smp 2' || \
  fail "M2C smoke must start two harts"

if QS_ROOT="$tmp" QS_QEMU="$tmp/fake-qemu" QS_QEMU_ARGS="$tmp/qemu.args" \
QS_FAKE_BAD_ITERATION=1 QS_EXTRA_MARKERS='QS:FATFS_ITERATIONS:128' \
QS_SMOKE_TIMEOUT=2 "$root/scripts/m2c-smoke.sh" \
  >"$tmp/iteration.out" 2>"$tmp/iteration.err"; then
  fail "extra markers must match a complete line"
fi

if QS_ROOT="$tmp" QS_QEMU="$tmp/fake-qemu" QS_QEMU_ARGS="$tmp/qemu.args" \
QS_FAKE_EXIT=7 QS_SMOKE_TIMEOUT=2 \
  "$root/scripts/m2c-smoke.sh" >"$tmp/bad.out" 2>"$tmp/bad.err"; then
  fail "nonzero guest exit must fail even when markers are complete"
fi
grep -qi 'exit' "$tmp/bad.err" || \
  fail "nonzero guest exit failure should be explicit"

if ! QS_ROOT="$tmp" QS_STAGE=m2c-stress QS_TEST_NAME=m2c-stress \
QS_QEMU="$tmp/fake-qemu" QS_QEMU_ARGS="$tmp/stress.args" QS_FAKE_STRESS=1 \
QS_SMOKE_TIMEOUT=2 "$root/scripts/m2c-smoke.sh" \
  >"$tmp/stress.out" 2>"$tmp/stress.err"; then
  cat "$tmp/stress.out" >&2
  cat "$tmp/stress.err" >&2
  fail "stress counters must tolerate binary serial output and CRLF"
fi

echo "PASS: M2C smoke script behavior"
