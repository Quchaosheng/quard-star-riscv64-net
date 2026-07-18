#!/usr/bin/env bash
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

build_and_run()
{
  name=$1
  stable=$2
  pending=$3
  retry=$4
  timer=$5
  cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined \
    -fno-sanitize-recover=undefined \
    -DARP_ENTRY_STABLE_TMO="$stable" \
    -DARP_ENTRY_PENDING_TMO="$pending" \
    -DARP_ENTRY_RETRY_CNT="$retry" -DARP_TIMER_TMO="$timer" \
    -pthread -I"$root/kernel/include" -I"$root/kernel/include/timeros/net" \
    "$root/tests/host/test_m6_arp_timer.c" \
    "$root/tests/host/net_host_port.c" \
    "$root/kernel/src/net/net_sys.c" \
    "$root/kernel/src/net/nlist.c" \
    "$root/kernel/src/net/nlocker.c" \
    "$root/kernel/src/net/mblock.c" \
    "$root/kernel/src/net/fixq.c" \
    "$root/kernel/src/net/pktbuf.c" \
    "$root/kernel/src/net/ipaddr.c" \
    "$root/kernel/src/net/netif.c" \
    "$root/kernel/src/net/tools.c" \
    "$root/kernel/src/net/timer.c" \
    "$root/kernel/src/net/ether.c" \
    "$root/kernel/src/net/arp.c" \
    -o "$tmp/$name"
  ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 \
    UBSAN_OPTIONS=halt_on_error=1 timeout 15s "$tmp/$name"
}

build_and_run test_m6_arp_timer 2 1 3 1
build_and_run test_m6_arp_timer_nondivisible 5 3 3 2

if cc -std=c11 -Wall -Wextra -Werror -DARP_TIMER_TMO=0 \
    -I"$root/kernel/include" -I"$root/kernel/include/timeros/net" \
    -c "$root/kernel/src/net/arp.c" -o "$tmp/invalid.o" \
    >"$tmp/invalid.log" 2>&1; then
  echo 'FAIL: zero ARP timer interval compiled successfully' >&2
  exit 1
fi
if ! grep -q 'ARP_TIMER_TMO must be positive' "$tmp/invalid.log"; then
  echo 'FAIL: zero ARP timer interval lacked configuration diagnostic' >&2
  exit 1
fi

echo 'PASS: M6 ARP aging, retry, and cleanup behavior'
