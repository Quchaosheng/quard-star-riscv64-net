#!/usr/bin/env bash
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

mkdir -p "$tmp/out/m2b/qemu" "$tmp/out/m2b/fw" "$tmp/out/m2b/disk"
: > "$tmp/out/m2b/fw/fw.bin"
: > "$tmp/out/m2b/disk/disk.img"

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

base='QS:BOOT_OK
QS:KERNEL_READY
QS:BLOCK_OK
QS:HART_ONLINE:0
QS:HART_ONLINE:1
QS:SMP_ALLOC_OK
QS:TEST_PASS:m2a-smoke'
case ${QS_FAKE_MODE:-complete} in
  complete)
    printf '%s\nQS:SMP_SCHED_OK\nQS:TEST_PASS:m2b-smoke\n' "$base" > "$kernel_log"
    ;;
  missing-scheduler)
    printf '%s\nQS:TEST_PASS:m2b-smoke\n' "$base" > "$kernel_log"
    ;;
esac
EOF
chmod +x "$tmp/fake-qemu"

if ! QS_ROOT="$tmp" QS_QEMU="$tmp/fake-qemu" QS_QEMU_ARGS="$tmp/qemu.args" \
QS_SMOKE_TIMEOUT=2 "$root/scripts/m2b-smoke.sh" >"$tmp/good.out" 2>"$tmp/good.err"; then
  cat "$tmp/good.out" >&2
  cat "$tmp/good.err" >&2
  fail "complete M2B marker set should pass"
fi

paste -sd ' ' "$tmp/qemu.args" | grep -Fq -- '-smp 2' || \
  fail "M2B smoke must start two harts"

if QS_ROOT="$tmp" QS_QEMU="$tmp/fake-qemu" QS_QEMU_ARGS="$tmp/qemu.args" \
QS_FAKE_MODE=missing-scheduler QS_SMOKE_TIMEOUT=1 \
  "$root/scripts/m2b-smoke.sh" >"$tmp/bad.out" 2>"$tmp/bad.err"; then
  fail "missing scheduler marker should fail"
fi
grep -q 'QS:SMP_SCHED_OK' "$tmp/bad.err" || \
  fail "failure should name the missing scheduler marker"

echo "PASS: M2B smoke script behavior"
