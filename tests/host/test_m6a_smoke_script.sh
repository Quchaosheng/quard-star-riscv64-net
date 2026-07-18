#!/usr/bin/env bash
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

fail()
{
  echo "FAIL: $*" >&2
  exit 1
}

mkdir -p "$tmp/out/m6a/qemu" "$tmp/out/m6a/fw" "$tmp/out/m6a/disk"
: > "$tmp/out/m6a/fw/fw.bin"
: > "$tmp/out/m6a/disk/disk.img"

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
  if [ "${QS_FAKE_STATS:-complete}" = incomplete ]; then
    printf '{"raw_frames":%s,"arp_requests":1}\n' "$raw_count" > "$stats"
  else
    printf '{"raw_frames":%s,"arp_requests":1,"arp_replies":2,"guest_echo_requests":1,"guest_echo_replies":1,"host_echo_requests":1,"host_echo_replies":1}\n' "$raw_count" > "$stats"
  fi
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
  case "$arg" in file:*) kernel_log=${arg#file:} ;; esac
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
QS:NET_LINK_OK
QS:NET_IRQ_OK
QS:NET_TX_OK
QS:NET_RX_OK
QS:NET_RESET_OK
QS:NET_RESETS:1
QS:NET_STRESS_FRAMES:32
QS:M5_ARP_OK
QS:M5_PING_OK
QS:M6_QUEUE_OK
QS:M6_ARP_TIMER_OK
QS:M6_LOOP_OK
QS:TEST_PASS:m6a-smoke
LOG
case ${QS_FAKE_MODE:-complete} in
  missing-queue) sed -i '/QS:M6_QUEUE_OK/d' "$kernel_log" ;;
  missing-arp) sed -i '/QS:M6_ARP_TIMER_OK/d' "$kernel_log" ;;
  missing-loop) sed -i '/QS:M6_LOOP_OK/d' "$kernel_log" ;;
  duplicate-queue) printf 'QS:M6_QUEUE_OK\n' >> "$kernel_log" ;;
  duplicate-pass) printf 'QS:TEST_PASS:m6a-smoke\n' >> "$kernel_log" ;;
  guest-fail) printf 'QS:TEST_FAIL:m6a-net:fake\n' >> "$kernel_log" ;;
esac
exit "${QS_FAKE_QEMU_EXIT:-0}"
EOF
chmod +x "$tmp/fake-qemu"

cat > "$tmp/python3" <<'EOF'
#!/usr/bin/env bash
set -eu
[ "$1" = - ]
stats=$2
expected=$3
grep -q "\"raw_frames\":$expected" "$stats"
for key in arp_requests arp_replies guest_echo_requests \
  guest_echo_replies host_echo_requests host_echo_replies; do
  grep -Eq "\"$key\":[1-9][0-9]*" "$stats"
done
EOF
chmod +x "$tmp/python3"

run_m6a()
{
  PATH="$tmp:$PATH" QS_ROOT="$tmp" QS_QEMU="$tmp/fake-qemu" \
  QS_M5_PEER="$tmp/fake-peer" \
  QS_SUDO="$tmp/fake-sudo" QS_FORCE_PEER_SUDO=1 QS_TAP_MANAGED=0 \
  QS_TAP_IFACE=tap-test QS_SMOKE_TIMEOUT=2 "$root/scripts/m6a-smoke.sh"
}

if ! run_m6a >"$tmp/good.out" 2>"$tmp/good.err"; then
  cat "$tmp/good.err" >&2
  fail 'complete M6A acceptance must pass'
fi
for mode in missing-queue missing-arp missing-loop duplicate-queue \
  duplicate-pass guest-fail; do
  if QS_FAKE_MODE=$mode run_m6a >"$tmp/$mode.out" 2>"$tmp/$mode.err"; then
    fail "$mode must fail"
  fi
done
if QS_FAKE_QEMU_EXIT=7 run_m6a >"$tmp/qemu.out" 2>"$tmp/qemu.err"; then
  fail 'nonzero QEMU exit must fail'
fi
if QS_FAKE_PEER_EXIT=9 run_m6a >"$tmp/peer.out" 2>"$tmp/peer.err"; then
  fail 'nonzero peer exit must fail'
fi
if QS_FAKE_STATS=incomplete run_m6a >"$tmp/stats.out" 2>"$tmp/stats.err"; then
  fail 'incomplete peer stats must fail'
fi

echo 'PASS: M6A smoke script behavior'
