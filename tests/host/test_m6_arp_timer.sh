#!/usr/bin/env bash
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined \
  -fno-sanitize-recover=undefined \
  -DARP_ENTRY_STABLE_TMO=2 -DARP_ENTRY_PENDING_TMO=1 \
  -DARP_ENTRY_RETRY_CNT=3 -DARP_TIMER_TMO=1 \
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
  -o "$tmp/test_m6_arp_timer"
ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 \
  UBSAN_OPTIONS=halt_on_error=1 \
  timeout 15s "$tmp/test_m6_arp_timer"

echo 'PASS: M6 ARP aging, retry, and cleanup behavior'
