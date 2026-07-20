#!/usr/bin/env bash
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -DSOCKET_TEST \
  -fno-sanitize-recover=all -pthread \
  -I"$root/kernel/include" -I"$root/kernel/include/timeros/net" \
  "$root/tests/host/test_m6c1_socket.c" \
  "$root/tests/host/net_host_port.c" \
  "$root/kernel/src/net/net_sys.c" \
  "$root/kernel/src/net/nlist.c" \
  "$root/kernel/src/net/nlocker.c" \
  "$root/kernel/src/net/mblock.c" \
  "$root/kernel/src/net/fixq.c" \
  "$root/kernel/src/net/pktbuf.c" \
  "$root/kernel/src/net/ipaddr.c" \
  "$root/kernel/src/net/tools.c" \
  "$root/kernel/src/net/timer.c" \
  "$root/kernel/src/net/udp.c" \
  "$root/kernel/src/net/tcp.c" \
  "$root/kernel/src/net/socket.c" \
  -o "$tmp/test_m6c1_socket"
ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 \
  UBSAN_OPTIONS=halt_on_error=1 "$tmp/test_m6c1_socket"

echo 'PASS: M6C1 TCP socket handle and type behavior'
