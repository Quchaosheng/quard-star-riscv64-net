#!/usr/bin/env bash
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

mkdir -p "$tmp/out/m1/qemu" "$tmp/out/m1/fw" "$tmp/out/m1/disk"
: > "$tmp/out/m1/fw/fw.bin"
: > "$tmp/out/m1/disk/disk.img"

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

case ${QS_FAKE_MODE:-complete} in
  complete)
    printf 'QS:BOOT_OK\nQS:KERNEL_READY\nQS:BLOCK_OK\nQS:TEST_PASS:m1-smoke\n' > "$kernel_log"
    ;;
  missing-kernel)
    printf 'QS:BOOT_OK\nQS:BLOCK_OK\nQS:TEST_PASS:m1-smoke\n' > "$kernel_log"
    ;;
  missing-block)
    printf 'QS:BOOT_OK\nQS:KERNEL_READY\nQS:TEST_PASS:m1-smoke\n' > "$kernel_log"
    ;;
esac
EOF
chmod +x "$tmp/fake-qemu"

if ! QS_ROOT="$tmp" QS_QEMU="$tmp/fake-qemu" QS_QEMU_ARGS="$tmp/qemu.args" \
QS_SMOKE_TIMEOUT=2 "$root/scripts/m1-smoke.sh" >"$tmp/good.out" 2>"$tmp/good.err"; then
  cat "$tmp/good.out" >&2
  cat "$tmp/good.err" >&2
  fail "single-hart marker set should pass"
fi

paste -sd ' ' "$tmp/qemu.args" | grep -Fq -- '-smp 1' || \
  fail "smoke must start one hart"

if QS_ROOT="$tmp" QS_QEMU="$tmp/fake-qemu" QS_QEMU_ARGS="$tmp/qemu.args" \
QS_FAKE_MODE=missing-kernel QS_SMOKE_TIMEOUT=1 \
  "$root/scripts/m1-smoke.sh" >"$tmp/bad.out" 2>"$tmp/bad.err"; then
  fail "missing kernel marker should fail"
fi
grep -q 'QS:KERNEL_READY' "$tmp/bad.err" || fail "failure should name missing marker"

if QS_ROOT="$tmp" QS_QEMU="$tmp/fake-qemu" QS_QEMU_ARGS="$tmp/qemu.args" \
QS_FAKE_MODE=missing-block QS_SMOKE_TIMEOUT=1 \
  "$root/scripts/m1-smoke.sh" >"$tmp/block.out" 2>"$tmp/block.err"; then
  fail "missing block marker should fail"
fi
grep -q 'QS:BLOCK_OK' "$tmp/block.err" || fail "failure should name missing block marker"

echo "PASS: M1 smoke script behavior"
