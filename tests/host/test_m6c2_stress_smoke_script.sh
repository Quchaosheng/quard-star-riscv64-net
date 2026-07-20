#!/usr/bin/env bash
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
fail() { echo "FAIL: $*" >&2; exit 1; }

[ -x "$root/scripts/m6c2-stress.sh" ] || fail 'missing m6c2-stress script'
mkdir -p "$tmp/out/m6c2-stress/qemu" "$tmp/out/m6c2-stress/fw" \
  "$tmp/out/m6c2-stress/disk"
cp -a "$root/scripts" "$tmp/scripts"
: > "$tmp/out/m6c2-stress/fw/fw.bin"
: > "$tmp/out/m6c2-stress/disk/disk.img"

cat > "$tmp/scripts/m6c2-build.sh" <<'EOF'
#!/usr/bin/env bash
set -eu
[ "${QS_STAGE:-}" = m6c2-stress ]
[ "${QS_M6C2_STRESS:-0}" = 1 ]
printf 'built\n' > "$QS_ROOT/stress-built"
EOF
chmod +x "$tmp/scripts/m6c2-build.sh"

cat > "$tmp/fake-peer" <<'EOF'
#!/usr/bin/env bash
set -eu
ready=
stats=
raw_count=32
stress=0
while [ "$#" -gt 0 ]; do
  case "$1" in
    --ready-file) ready=$2; shift 2 ;;
    --stats-file) stats=$2; shift 2 ;;
    --raw-count) raw_count=$2; shift 2 ;;
    --require-tcp-server-stress) stress=1; shift ;;
    *) shift ;;
  esac
done
[ "$stress" -eq 1 ]
printf 'ready\n' > "$ready"
handshakes=108
echoes=108
peak=8
reconnects=100
fin=108
outstanding=0
case ${QS_FAKE_STATS:-complete} in
  handshakes) handshakes=107 ;;
  echo) echoes=107 ;;
  peak) peak=7 ;;
  reconnects) reconnects=99 ;;
  fin) fin=107 ;;
  outstanding) outstanding=1 ;;
esac
printf '{"raw_frames":%s,"arp_requests":1,"arp_replies":2,"guest_echo_requests":1,"guest_echo_replies":1,"host_echo_requests":1,"host_echo_replies":1,"udp_requests":1,"udp_replies":1,"tcp_syn":1,"tcp_data":1,"tcp_retransmissions":1,"tcp_fin":1,"tcp_outstanding":0,"tcp_server_stress_handshakes":%s,"tcp_server_stress_echo":%s,"tcp_server_stress_parallel_peak":%s,"tcp_server_stress_reconnects":%s,"tcp_server_stress_fin":%s,"tcp_server_stress_outstanding":%s}\n' \
  "$raw_count" "$handshakes" "$echoes" "$peak" "$reconnects" "$fin" \
  "$outstanding" > "$stats"
exit "${QS_FAKE_PEER_EXIT:-0}"
EOF
chmod +x "$tmp/fake-peer"

cat > "$tmp/fake-qemu" <<'EOF'
#!/usr/bin/env bash
set -eu
kernel_log=
for arg in "$@"; do
  case "$arg" in file:*) kernel_log=${arg#file:} ;; esac
done
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
QS:M6B_UDP_OK
QS:M6B_UDP_TIMEOUT_OK
QS:M6C1_TCP_OK
QS:M6C1_TCP_RETRANS_OK
QS:M6C1_TCP_CLOSE_OK
QS:M6C2_LISTEN_OK
QS:M6C2_ACCEPT_OK
QS:M6C2_ECHO_OK
QS:M6C2_CLOSE_OK
QS:M6C2_STRESS_PARALLEL_OK
QS:M6C2_STRESS_RECONNECT_OK
QS:STRESS_ALLOC_OPS:100000
QS:STRESS_MIGRATIONS:10000
QS:STRESS_ELAPSED_TICKS:1200000000
QS:TEST_PASS:m6c2-stress
LOG
case ${QS_FAKE_MODE:-complete} in
  missing) sed -i '/QS:M6C2_STRESS_PARALLEL_OK/d' "$kernel_log" ;;
  duplicate) printf 'QS:M6C2_STRESS_RECONNECT_OK\n' >> "$kernel_log" ;;
  early)
    sed -i '/QS:TEST_PASS:m6c2-stress/d' "$kernel_log"
    sed -i '/QS:M6C2_STRESS_PARALLEL_OK/i QS:TEST_PASS:m6c2-stress' "$kernel_log"
    ;;
  guest-fail) printf 'QS:TEST_FAIL:m6c2-stress\n' >> "$kernel_log" ;;
esac
exit "${QS_FAKE_QEMU_EXIT:-0}"
EOF
chmod +x "$tmp/fake-qemu"

run_stress()
{
  PATH="$tmp:$PATH" QS_ROOT="$tmp" QS_QEMU="$tmp/fake-qemu" \
  QS_M5_PEER="$tmp/fake-peer" QS_TAP_MANAGED=0 QS_TAP_IFACE=tap-test \
  QS_SMOKE_TIMEOUT=2 "$root/scripts/m6c2-stress.sh"
}

run_stress > "$tmp/good.out" 2> "$tmp/good.err" || {
  cat "$tmp/good.err" >&2
  fail 'complete M6C2 stress acceptance must pass'
}
[ -s "$tmp/stress-built" ] || fail 'stress wrapper skipped cumulative build'

for mode in missing duplicate early guest-fail; do
  if QS_FAKE_MODE=$mode run_stress > "$tmp/$mode.out" 2> "$tmp/$mode.err"; then
    fail "$mode stress marker case must fail"
  fi
done
for mode in handshakes echo peak reconnects fin outstanding; do
  if QS_FAKE_STATS=$mode run_stress > "$tmp/stats-$mode.out" \
      2> "$tmp/stats-$mode.err"; then
    fail "$mode stress statistics must fail"
  fi
done
if QS_FAKE_QEMU_EXIT=7 run_stress > "$tmp/qemu.out" 2> "$tmp/qemu.err"; then
  fail 'nonzero stress QEMU exit must fail'
fi
if QS_FAKE_PEER_EXIT=9 run_stress > "$tmp/peer.out" 2> "$tmp/peer.err"; then
  fail 'nonzero stress peer exit must fail'
fi

echo 'PASS: M6C2 TCP stress smoke script behavior'
