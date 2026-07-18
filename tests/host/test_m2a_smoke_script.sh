#!/usr/bin/env bash
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

mkdir -p "$tmp/out/m2a/qemu" "$tmp/out/m2a/fw" "$tmp/out/m2a/disk"
: > "$tmp/out/m2a/fw/fw.bin"
: > "$tmp/out/m2a/disk/disk.img"

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
    printf 'QS:BOOT_OK\nQS:KERNEL_READY\nQS:BLOCK_OK\nQS:HART_ONLINE:0\nQS:HART_ONLINE:1\nQS:SMP_ALLOC_OK\nQS:TEST_PASS:m2a-smoke\n' > "$kernel_log"
    ;;
  missing-hart1)
    printf 'QS:BOOT_OK\nQS:KERNEL_READY\nQS:BLOCK_OK\nQS:HART_ONLINE:0\nQS:SMP_ALLOC_OK\nQS:TEST_PASS:m2a-smoke\n' > "$kernel_log"
    ;;
esac
EOF
chmod +x "$tmp/fake-qemu"

if ! QS_ROOT="$tmp" QS_QEMU="$tmp/fake-qemu" QS_QEMU_ARGS="$tmp/qemu.args" \
QS_SMOKE_TIMEOUT=2 "$root/scripts/m2a-smoke.sh" >"$tmp/good.out" 2>"$tmp/good.err"; then
  cat "$tmp/good.out" >&2
  cat "$tmp/good.err" >&2
  fail "dual-hart marker set should pass"
fi

paste -sd ' ' "$tmp/qemu.args" | grep -Fq -- '-smp 2' || \
  fail "M2A smoke must start two harts"

if QS_ROOT="$tmp" QS_QEMU="$tmp/fake-qemu" QS_QEMU_ARGS="$tmp/qemu.args" \
QS_FAKE_MODE=missing-hart1 QS_SMOKE_TIMEOUT=1 \
  "$root/scripts/m2a-smoke.sh" >"$tmp/bad.out" 2>"$tmp/bad.err"; then
  fail "missing hart 1 marker should fail"
fi
grep -q 'QS:HART_ONLINE:1' "$tmp/bad.err" || \
  fail "failure should name the missing hart 1 marker"

echo "PASS: M2A smoke script behavior"
