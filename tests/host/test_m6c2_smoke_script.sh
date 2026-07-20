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

for marker in QS:M6C2_LISTEN_OK QS:M6C2_ACCEPT_OK QS:M6C2_ECHO_OK \
  QS:M6C2_CLOSE_OK; do
  grep -q "$marker" "$root/scripts/m5-smoke.sh" || \
    fail "missing $marker smoke requirement"
done
for key in tcp_server_syn tcp_server_handshakes tcp_server_data \
  tcp_server_echo tcp_server_retransmissions tcp_server_fin \
  tcp_server_outstanding; do
  grep -q "$key" "$root/scripts/m5-peer.py" || \
    fail "missing peer statistic $key"
done
grep -q -- '--require-tcp-server' "$root/scripts/m5-peer.py" || \
  fail 'peer must expose passive TCP server mode'
grep -q 'tcp_server_echo' "$root/user/Makefile" || \
  fail 'missing TCP server Echo build target'

mkdir -p "$tmp/out/m6c2/qemu" "$tmp/out/m6c2/fw" "$tmp/out/m6c2/disk"
cp -a "$root/scripts" "$tmp/scripts"
: > "$tmp/out/m6c2/fw/fw.bin"
: > "$tmp/out/m6c2/disk/disk.img"

cat > "$tmp/fake-peer" <<'EOF'
#!/usr/bin/env bash
set -eu
ready=
stats=
raw_count=32
server=0
while [ "$#" -gt 0 ]; do
  case "$1" in
    --ready-file) ready=$2; shift 2 ;;
    --stats-file) stats=$2; shift 2 ;;
    --raw-count) raw_count=$2; shift 2 ;;
    --require-tcp-server) server=1; shift ;;
    *) shift ;;
  esac
done
[ "$server" -eq 1 ]
[ -n "$ready" ] && printf 'ready\n' > "$ready"
if [ -n "$stats" ]; then
  case ${QS_FAKE_STATS:-complete} in
    incomplete)
      printf '{"raw_frames":%s,"arp_requests":1}\n' "$raw_count" > "$stats"
      ;;
    missing)
      printf '{"raw_frames":%s,"arp_requests":1,"arp_replies":2,"guest_echo_requests":1,"guest_echo_replies":1,"host_echo_requests":1,"host_echo_replies":1,"udp_requests":1,"udp_replies":1,"tcp_syn":1,"tcp_data":1,"tcp_retransmissions":1,"tcp_fin":1,"tcp_outstanding":0,"tcp_server_syn":1,"tcp_server_handshakes":1,"tcp_server_data":1,"tcp_server_echo":1,"tcp_server_retransmissions":1,"tcp_server_fin":1}\n' "$raw_count" > "$stats"
      ;;
    zero)
      printf '{"raw_frames":%s,"arp_requests":1,"arp_replies":2,"guest_echo_requests":1,"guest_echo_replies":1,"host_echo_requests":1,"host_echo_replies":1,"udp_requests":1,"udp_replies":1,"tcp_syn":1,"tcp_data":1,"tcp_retransmissions":1,"tcp_fin":1,"tcp_outstanding":0,"tcp_server_syn":1,"tcp_server_handshakes":1,"tcp_server_data":1,"tcp_server_echo":0,"tcp_server_retransmissions":1,"tcp_server_fin":1,"tcp_server_outstanding":0}\n' "$raw_count" > "$stats"
      ;;
    *)
      printf '{"raw_frames":%s,"arp_requests":1,"arp_replies":2,"guest_echo_requests":1,"guest_echo_replies":1,"host_echo_requests":1,"host_echo_replies":1,"udp_requests":1,"udp_replies":1,"tcp_syn":1,"tcp_data":1,"tcp_retransmissions":1,"tcp_fin":1,"tcp_outstanding":0,"tcp_server_syn":1,"tcp_server_handshakes":1,"tcp_server_data":1,"tcp_server_echo":1,"tcp_server_retransmissions":1,"tcp_server_fin":1,"tcp_server_outstanding":0}\n' "$raw_count" > "$stats"
      ;;
  esac
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
QS:M6B_UDP_OK
QS:M6B_UDP_TIMEOUT_OK
QS:M6C1_TCP_OK
QS:M6C1_TCP_RETRANS_OK
QS:M6C1_TCP_CLOSE_OK
QS:M6C2_LISTEN_OK
QS:M6C2_ACCEPT_OK
QS:M6C2_ECHO_OK
QS:M6C2_CLOSE_OK
QS:TEST_PASS:m6c2-smoke
LOG
case ${QS_FAKE_MODE:-complete} in
  missing-marker) sed -i '/QS:M6C2_ECHO_OK/d' "$kernel_log" ;;
  duplicate-marker) printf 'QS:M6C2_ACCEPT_OK\n' >> "$kernel_log" ;;
  duplicate-pass) printf 'QS:TEST_PASS:m6c2-smoke\n' >> "$kernel_log" ;;
  late-close)
    sed -i '/QS:M6C2_CLOSE_OK/d' "$kernel_log"
    printf 'QS:M6C2_CLOSE_OK\n' >> "$kernel_log"
    ;;
  guest-fail) printf 'QS:TEST_FAIL:m6c2-recv\n' >> "$kernel_log" ;;
esac
exit "${QS_FAKE_QEMU_EXIT:-0}"
EOF
chmod +x "$tmp/fake-qemu"

validator=$(PATH="$tmp:$PATH" command -v python3 || true)
[ -n "$validator" ] || fail 'python3 is required for smoke stats validation'
case "$validator" in
  "$tmp"/*) fail 'smoke test must exercise the production Python validator' ;;
esac

run_m6c2()
{
  PATH="$tmp:$PATH" QS_ROOT="$tmp" QS_QEMU="$tmp/fake-qemu" \
  QS_M5_PEER="$tmp/fake-peer" QS_SUDO="$tmp/fake-sudo" \
  QS_FORCE_PEER_SUDO=1 QS_TAP_MANAGED=0 QS_TAP_IFACE=tap-test \
  QS_SMOKE_TIMEOUT=2 "$root/scripts/m6c2-smoke.sh"
}

if ! run_m6c2 >"$tmp/good.out" 2>"$tmp/good.err"; then
  cat "$tmp/good.err" >&2
  fail 'complete M6C2 acceptance must pass'
fi
for mode in missing-marker duplicate-marker duplicate-pass late-close guest-fail; do
  if QS_FAKE_MODE=$mode run_m6c2 >"$tmp/$mode.out" 2>"$tmp/$mode.err"; then
    fail "$mode must fail"
  fi
done
for mode in incomplete missing zero; do
  if QS_FAKE_STATS=$mode run_m6c2 >"$tmp/stats-$mode.out" \
      2>"$tmp/stats-$mode.err"; then
    fail "$mode peer statistics must fail"
  fi
done
if QS_FAKE_QEMU_EXIT=7 run_m6c2 >"$tmp/qemu.out" 2>"$tmp/qemu.err"; then
  fail 'nonzero QEMU exit must fail'
fi
if QS_FAKE_PEER_EXIT=9 run_m6c2 >"$tmp/peer.out" 2>"$tmp/peer.err"; then
  fail 'nonzero peer exit must fail'
fi

echo 'PASS: M6C2 passive TCP smoke script behavior'
