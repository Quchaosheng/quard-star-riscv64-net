#!/usr/bin/env bash
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

mkdir -p "$tmp/out/m5/qemu" "$tmp/out/m5/fw" "$tmp/out/m5/disk"
: > "$tmp/out/m5/fw/fw.bin"
: > "$tmp/out/m5/disk/disk.img"

cat > "$tmp/fake-peer" <<'EOF'
#!/usr/bin/env bash
set -eu
ready=
stats=
raw_count=32
while [ "$#" -gt 0 ]; do
  case "$1" in
    --ready-file) ready=$2; shift 2 ;;
    --stats-file) stats=$2; shift 2 ;;
    --raw-count) raw_count=$2; shift 2 ;;
    *) shift ;;
  esac
done
[ -n "$ready" ] && printf 'ready\n' > "$ready"
if [ -n "$stats" ]; then
  printf '{"raw_frames":%s,"arp_requests":1,"arp_replies":2,"guest_echo_requests":1,"guest_echo_replies":1,"host_echo_requests":1,"host_echo_replies":1,"elapsed_seconds":0.1}\n' "$raw_count" > "$stats"
fi
exit "${QS_FAKE_PEER_EXIT:-0}"
EOF
chmod +x "$tmp/fake-peer"

cat > "$tmp/fake-sudo" <<'EOF'
#!/usr/bin/env bash
set -eu
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
QS:M5_ARP_OK
QS:M5_PING_OK
QS:TEST_PASS:m5-smoke
LOG
case ${QS_FAKE_MODE:-complete} in
  missing-ping) sed -i '/QS:M5_PING_OK/d' "$kernel_log" ;;
  failure) printf 'QS:TEST_FAIL:m5-net:fake\n' >> "$kernel_log" ;;
esac
exit "${QS_FAKE_EXIT:-0}"
EOF
chmod +x "$tmp/fake-qemu"

run_m5() {
  QS_ROOT="$tmp" QS_QEMU="$tmp/fake-qemu" \
  QS_QEMU_ARGS="$tmp/qemu.args" QS_M5_PEER="$tmp/fake-peer" \
  QS_SUDO="$tmp/fake-sudo" QS_FORCE_PEER_SUDO=1 \
  QS_TAP_MANAGED=0 QS_TAP_IFACE=tap-test QS_SMOKE_TIMEOUT=2 \
    "$root/scripts/m5-smoke.sh"
}

run_m5 >"$tmp/good.out" 2>"$tmp/good.err" || \
  fail "complete M5 markers, peer, and guest exit zero should pass"
grep -Fxq -- '-global' "$tmp/qemu.args" || fail "missing QEMU global option"
grep -Fxq 'virtio-mmio.force-legacy=true' "$tmp/qemu.args" || \
  fail "VirtIO net must use the legacy transport"
grep -Fxq 'tap,id=net0,ifname=tap-test,script=no,downscript=no' \
  "$tmp/qemu.args" || fail "QEMU must bind net0 to tap-test"

if QS_FAKE_MODE=missing-ping run_m5 >"$tmp/missing.out" 2>"$tmp/missing.err"; then
  fail "missing M5 marker must fail"
fi
grep -Fq 'QS:M5_PING_OK' "$tmp/missing.err" || \
  fail "missing M5 marker failure should name QS:M5_PING_OK"

export QS_FAKE_PEER_EXIT=9
if run_m5 >"$tmp/peer.out" 2>"$tmp/peer.err"; then
  cat "$tmp/peer.err" >&2
  unset QS_FAKE_PEER_EXIT
  fail "nonzero M5 peer exit must fail"
fi
unset QS_FAKE_PEER_EXIT

echo 'PASS: M5 smoke script behavior'
