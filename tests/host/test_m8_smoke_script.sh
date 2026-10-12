#!/usr/bin/env bash
set -eu

# Fixture-driven behavior checks for the M8 runtime tier. The fake QEMU and
# fake TAP peer replay serial output, so the marker gate is exercised without a
# RISC-V toolchain or hardware virtualization.

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

# The M8 smoke delegates to the shared staged smoke scripts, so the fixture
# root must expose the real scripts/ tree.
mkdir -p "$tmp/out/m8/qemu" "$tmp/out/m8/fw" "$tmp/out/m8/disk"
: > "$tmp/out/m8/fw/fw.bin"
: > "$tmp/out/m8/disk/disk.img"
ln -s "$root/scripts" "$tmp/scripts"

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
cat > "$stats" <<STATS
{
  "raw_frames": $raw_count,
  "arp_requests": 1, "arp_replies": 2,
  "guest_echo_requests": 1, "guest_echo_replies": 1,
  "host_echo_requests": 1, "host_echo_replies": 1,
  "udp_requests": 1, "udp_replies": 1,
  "dns_queries": 2, "dns_replies": 1, "dns_timeouts": 1,
  "http_requests": 1, "http_responses": 1, "http_outstanding": 0,
  "ntp_queries": 2, "ntp_replies": 1, "ntp_timeouts": 1,
  "tftp_rrq": 1, "tftp_data": 2049, "tftp_acks": 2049,
  "tftp_bytes": 1048576, "tftp_timeouts": 1, "tftp_outstanding": 0,
  "elapsed_seconds": 0.1
}
STATS
exit "${QS_FAKE_PEER_EXIT:-0}"
EOF
chmod +x "$tmp/fake-peer"

cat > "$tmp/fake-sudo" <<'EOF'
#!/usr/bin/env bash
set -eu
exec "$@"
EOF
chmod +x "$tmp/fake-sudo"

# The kernel log carries the ordinary-hart markers; the trusted log carries the
# hart-7 FreeRTOS markers. Both are produced by the fixture, never by a grep of
# the source tree.
cat > "$tmp/fake-qemu" <<'EOF'
#!/usr/bin/env bash
set -eu
kernel_log=
trusted_log=
serial_seen=0
for arg in "$@"; do
  case "$arg" in
    file:*)
      if [ "$serial_seen" -eq 0 ]; then
        kernel_log=${arg#file:}
      else
        trusted_log=${arg#file:}
      fi
      serial_seen=$((serial_seen + 1))
      ;;
  esac
done
[ -n "$kernel_log" ] && [ -n "$trusted_log" ]
printf '%s\n' "$@" > "$QS_QEMU_ARGS"

cat > "$kernel_log" <<'LOG'
QS:BOOT_OK
QS:KERNEL_READY
QS:BLOCK_OK
QS:HART_ONLINE:0
QS:HART_ONLINE:1
QS:HART_ONLINE:2
QS:HART_ONLINE:3
QS:HART_ONLINE:4
QS:HART_ONLINE:5
QS:HART_ONLINE:6
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
QS:M7A_DNS_QUERY_OK
QS:M7A_DNS_RESOLVE_OK
QS:M7A_DNS_TIMEOUT_OK
QS:M7B_HTTP_DNS_OK
QS:M7B_HTTP_CONNECT_OK
QS:M7B_HTTP_RESPONSE_OK
QS:M7B_HTTP_CLOSE_OK
QS:M7C_NTP_QUERY_OK
QS:M7C_NTP_RESPONSE_OK
QS:M7C_NTP_TIMEOUT_OK
QS:M7E_TFTP_RRQ_OK
QS:M7E_TFTP_1M_OK
QS:M7E_TFTP_SHA256_OK
QS:M7E_TFTP_REOPEN_OK
QS:M7E_TFTP_TIMEOUT_OK
QS:PMP_UNTRUSTED_DENY_OK
QS:PMP_UNTRUSTED_LOAD_DENY_OK
QS:PMP_UNTRUSTED_STORE_DENY_OK
QS:PMP_UNTRUSTED_EXEC_DENY_OK
Domain1 Region1  : 0x00000000bf800000-0x00000000bfffffff ()
Domain2 Region1  : 0x00000000bf800000-0x00000000bfffffff (R,W,X)
Domain1 Region2  : 0x0000000010002000-0x0000000010002fff (I)
Domain2 Region2  : 0x0000000010002000-0x0000000010002fff (I,R,W)
QS:TEST_PASS:m8-smoke
LOG

cat > "$trusted_log" <<'LOG'
QS:TRUSTED_READY
QS:TRUSTED_SCHED_OK
QS:PMP_TRUSTED_DENY_OK
QS:PMP_TRUSTED_LOAD_DENY_OK
QS:PMP_TRUSTED_STORE_DENY_OK
QS:PMP_TRUSTED_EXEC_DENY_OK
LOG

case ${QS_FAKE_MODE:-complete} in
  # A hart that reaches the ordinary kernel is a runtime isolation failure the
  # host source contracts cannot detect: the sources still declare the same
  # domains and the same seven-hart split.
  hart7-escaped) printf 'QS:HART_ONLINE:7\n' >> "$kernel_log" ;;
  # A PMP window that silently became writable to the untrusted domain.
  writable-domain1)
    sed -i 's/Domain1 Region1  : 0x00000000bf800000-0x00000000bfffffff ()/Domain1 Region1  : 0x00000000bf800000-0x00000000bfffffff (R,W,X)/' \
      "$kernel_log"
    ;;
  missing-trusted) sed -i '/QS:PMP_TRUSTED_DENY_OK/d' "$trusted_log" ;;
  missing-untrusted) sed -i '/QS:PMP_UNTRUSTED_DENY_OK/d' "$kernel_log" ;;
  failure) printf 'QS:TEST_FAIL:m8-smoke:fake\n' >> "$kernel_log" ;;
esac
exit "${QS_FAKE_EXIT:-0}"
EOF
chmod +x "$tmp/fake-qemu"

run_m8() {
  QS_ROOT="$tmp" QS_QEMU="$tmp/fake-qemu" \
  QS_QEMU_ARGS="$tmp/qemu.args" QS_M5_PEER="$tmp/fake-peer" \
  QS_SUDO="$tmp/fake-sudo" QS_FORCE_PEER_SUDO=1 \
  QS_TAP_MANAGED=0 QS_TAP_IFACE=tap-test QS_SMOKE_TIMEOUT=5 \
    "$root/scripts/m8-smoke.sh"
}

run_m8 >"$tmp/good.out" 2>"$tmp/good.err" || {
  cat "$tmp/good.out" >&2
  cat "$tmp/good.err" >&2
  fail "complete M8 markers from fixture logs should pass"
}

# Runtime failures the host source-contract checks cannot see.
for mode in hart7-escaped writable-domain1 missing-trusted missing-untrusted; do
  if QS_FAKE_MODE=$mode run_m8 >"$tmp/$mode.out" 2>"$tmp/$mode.err"; then
    fail "runtime defect '$mode' must fail M8 acceptance"
  fi
done
grep -q 'hart7' "$tmp/hart7-escaped.err" || \
  fail "escaped trusted hart failure should name hart7"
grep -q 'QS:PMP_TRUSTED_DENY_OK' "$tmp/missing-trusted.err" || \
  fail "missing trusted PMP marker should be reported"

# Negative markers and nonzero guest status must never be accepted.
if QS_FAKE_MODE=failure run_m8 >"$tmp/fail.out" 2>"$tmp/fail.err"; then
  fail "QS:TEST_FAIL must fail M8 acceptance"
fi
if QS_FAKE_EXIT=7 run_m8 >"$tmp/exit.out" 2>"$tmp/exit.err"; then
  fail "nonzero guest exit must fail M8 acceptance"
fi
if QS_FAKE_PEER_EXIT=9 run_m8 >"$tmp/peer.out" 2>"$tmp/peer.err"; then
  fail "nonzero TAP peer exit must fail M8 acceptance"
fi

# Incomplete setup: an artifact the smoke needs is absent.
mv "$tmp/out/m8/disk/disk.img" "$tmp/out/m8/disk/disk.img.keep"
if run_m8 >"$tmp/setup.out" 2>"$tmp/setup.err"; then
  fail "incomplete cached setup must fail M8 acceptance"
fi
mv "$tmp/out/m8/disk/disk.img.keep" "$tmp/out/m8/disk/disk.img"

# Timeout: the guest never emits the pass marker and never exits.
cat > "$tmp/fake-qemu-hang" <<'EOF'
#!/usr/bin/env bash
set -eu
kernel_log=
serial_seen=0
for arg in "$@"; do
  case "$arg" in
    file:*)
      if [ "$serial_seen" -eq 0 ]; then kernel_log=${arg#file:}; fi
      serial_seen=$((serial_seen + 1))
      ;;
  esac
done
printf 'QS:BOOT_OK\n' > "$kernel_log"
sleep 30
EOF
chmod +x "$tmp/fake-qemu-hang"
if QS_ROOT="$tmp" QS_QEMU="$tmp/fake-qemu-hang" QS_QEMU_ARGS="$tmp/hang.args" \
  QS_M5_PEER="$tmp/fake-peer" QS_SUDO="$tmp/fake-sudo" QS_FORCE_PEER_SUDO=1 \
  QS_TAP_MANAGED=0 QS_TAP_IFACE=tap-test QS_SMOKE_TIMEOUT=2 \
  "$root/scripts/m8-smoke.sh" >"$tmp/hang.out" 2>"$tmp/hang.err"; then
  fail "a guest that never reaches the pass marker must time out and fail"
fi

echo 'PASS: M8 runtime tier fixture behavior'
