#!/usr/bin/env bash
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

mkdir -p "$tmp/out/m4/qemu" "$tmp/out/m4/fw" "$tmp/out/m4/disk"
: > "$tmp/out/m4/fw/fw.bin"
: > "$tmp/out/m4/disk/disk.img"

cat > "$tmp/fake-peer" <<'EOF'
#!/usr/bin/env bash
set -eu
ready=
stats=
while [ "$#" -gt 0 ]; do
  case "$1" in
    --ready-file) ready=$2; shift 2 ;;
    --stats-file) stats=$2; shift 2 ;;
    *) shift ;;
  esac
done
[ -n "$ready" ] && printf 'ready\n' > "$ready"
[ -n "$stats" ] && printf '{"frames":%s,"elapsed_seconds":0.1}\n' \
  "${QS_FAKE_PEER_FRAMES:-32}" > "$stats"
exit "${QS_FAKE_PEER_EXIT:-0}"
EOF
chmod +x "$tmp/fake-peer"

cat > "$tmp/fake-sudo" <<'EOF'
#!/usr/bin/env bash
set -eu
printf 'sudo %s\n' "$*" >>"$QS_SUDO_LOG"
exec "$@"
EOF
chmod +x "$tmp/fake-sudo"

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
QS:VIRTQUEUE_OK
QS:BLOCK_IRQ_OK
QS:BLOCK_STRESS_OK
QS:FATFS_OK
QS:NET_LINK_OK
QS:NET_IRQ_OK
QS:NET_TX_OK
QS:NET_RX_OK
QS:NET_RESET_OK
QS:NET_RESETS:1
QS:NET_STRESS_FRAMES:32
QS:TEST_PASS:m4-smoke
LOG

case ${QS_FAKE_MODE:-complete} in
  missing-rx) sed -i '/QS:NET_RX_OK/d' "$kernel_log" ;;
  malformed-count)
    sed -i 's/QS:NET_STRESS_FRAMES:32/QS:NET_STRESS_FRAMES:320/' "$kernel_log"
    ;;
  failure) printf 'QS:TEST_FAIL:m4-net:fake\n' >> "$kernel_log" ;;
esac
exit "${QS_FAKE_EXIT:-0}"
EOF
chmod +x "$tmp/fake-qemu"

run_m4() {
  QS_ROOT="$tmp" QS_QEMU="$tmp/fake-qemu" \
  QS_QEMU_ARGS="$tmp/qemu.args" QS_M4_PEER="$tmp/fake-peer" \
  QS_SUDO="$tmp/fake-sudo" QS_SUDO_LOG="$tmp/sudo.log" \
  QS_TAP_MANAGED=0 QS_TAP_IFACE=tap-test QS_SMOKE_TIMEOUT=2 \
    "$root/scripts/m4-smoke.sh"
}

run_m4 >"$tmp/good.out" 2>"$tmp/good.err" || \
  fail "complete M4 markers, peer, and guest exit zero should pass"
grep -Fxq -- '-global' "$tmp/qemu.args" || fail "missing QEMU global option"
grep -Fxq 'virtio-mmio.force-legacy=true' "$tmp/qemu.args" || \
  fail "VirtIO net must use the legacy transport"
grep -Fxq -- '-netdev' "$tmp/qemu.args" || fail "missing QEMU netdev option"
grep -Fxq 'tap,id=net0,ifname=tap-test,script=no,downscript=no' \
  "$tmp/qemu.args" || fail "QEMU must bind net0 to tap-test"
grep -Fxq -- '-device' "$tmp/qemu.args" || fail "missing QEMU device option"
grep -Fxq 'virtio-net-device,netdev=net0,mac=52:54:00:12:34:56,bus=virtio-mmio-bus.1' \
  "$tmp/qemu.args" || fail "VirtIO net must use MMIO bus 1 and the fixed MAC"

QS_FORCE_PEER_SUDO=1 run_m4 >"$tmp/sudo.out" 2>"$tmp/sudo.err" || \
  fail "sudo raw peer fallback should pass"
grep -Fq "sudo $tmp/fake-peer tap-test" "$tmp/sudo.log" || \
  fail "raw peer fallback must use the configured sudo command"

if QS_FAKE_MODE=missing-rx run_m4 >"$tmp/missing.out" 2>"$tmp/missing.err"; then
  fail "missing M4 marker must fail"
fi
grep -Fq 'QS:NET_RX_OK' "$tmp/missing.err" || \
  fail "missing marker failure should name QS:NET_RX_OK"

if QS_FAKE_MODE=malformed-count run_m4 \
  >"$tmp/count.out" 2>"$tmp/count.err"; then
  fail "M4 frame count must match a complete line"
fi

if QS_FAKE_MODE=failure run_m4 >"$tmp/fail.out" 2>"$tmp/fail.err"; then
  fail "QS:TEST_FAIL must fail M4 acceptance"
fi

if QS_FAKE_EXIT=7 run_m4 >"$tmp/exit.out" 2>"$tmp/exit.err"; then
  fail "nonzero guest exit must fail"
fi
grep -qi 'exit' "$tmp/exit.err" || \
  fail "nonzero guest exit failure should be explicit"

if QS_FAKE_PEER_EXIT=9 run_m4 >"$tmp/peer.out" 2>"$tmp/peer.err"; then
  fail "nonzero TAP peer exit must fail"
fi

if QS_FAKE_PEER_FRAMES=31 run_m4 >"$tmp/frames.out" 2>"$tmp/frames.err"; then
  fail "peer frame count mismatch must fail"
fi

echo "PASS: M4 smoke script behavior"
