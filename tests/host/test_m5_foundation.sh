#!/usr/bin/env bash
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined \
  -pthread -I"$root/kernel/include" -I"$root/kernel/include/timeros/net" \
  "$root/tests/host/test_m5_foundation.c" \
  "$root/tests/host/net_host_port.c" \
  "$root/kernel/src/net/nlist.c" \
  "$root/kernel/src/net/nlocker.c" \
  "$root/kernel/src/net/mblock.c" \
  "$root/kernel/src/net/fixq.c" \
  "$root/kernel/src/net/pktbuf.c" \
  -o "$tmp/test_m5_foundation"
"$tmp/test_m5_foundation"

echo 'PASS: M5 foundation ownership and pktbuf behavior'
