#!/usr/bin/env bash
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined \
  -fno-sanitize-recover=all \
  -pthread -I"$root/kernel/include" -I"$root/kernel/include/timeros/net" \
  "$root/tests/host/test_m6_loop.c" \
  "$root/tests/host/net_host_port.c" \
  "$root/kernel/src/net/net_sys.c" \
  "$root/kernel/src/net/nlist.c" \
  "$root/kernel/src/net/nlocker.c" \
  "$root/kernel/src/net/mblock.c" \
  "$root/kernel/src/net/fixq.c" \
  "$root/kernel/src/net/pktbuf.c" \
  "$root/kernel/src/net/ipaddr.c" \
  "$root/kernel/src/net/netif.c" \
  "$root/kernel/src/net/netif_virtio.c" \
  "$root/kernel/src/net/tools.c" \
  "$root/kernel/src/net/timer.c" \
  "$root/kernel/src/net/ether.c" \
  "$root/kernel/src/net/arp.c" \
  "$root/kernel/src/net/ipv4.c" \
  "$root/kernel/src/net/icmpv4.c" \
  "$root/kernel/src/net/loop.c" \
  "$root/kernel/src/net/net_stack.c" \
  "$root/kernel/src/net/net_exec.c" \
  "$root/kernel/src/net/udp.c" \
  "$root/kernel/src/net/tcp.c" \
  "$root/kernel/src/net/socket.c" \
  -o "$tmp/test_m6_loop"
ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 \
  UBSAN_OPTIONS=halt_on_error=1 timeout 15s "$tmp/test_m6_loop"

echo 'PASS: M6 IPv4 loopback behavior'
