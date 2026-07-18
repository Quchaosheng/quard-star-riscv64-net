#!/usr/bin/env bash
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined \
  -pthread -I"$root/kernel/include" -I"$root/kernel/include/timeros/net" \
  "$root/tests/host/test_m5_arp.c" \
  "$root/tests/host/net_host_port.c" \
  "$root/kernel/src/net/nlist.c" \
  "$root/kernel/src/net/nlocker.c" \
  "$root/kernel/src/net/mblock.c" \
  "$root/kernel/src/net/fixq.c" \
  "$root/kernel/src/net/pktbuf.c" \
  "$root/kernel/src/net/ipaddr.c" \
  "$root/kernel/src/net/netif.c" \
  "$root/kernel/src/net/tools.c" \
  "$root/kernel/src/net/ether.c" \
  "$root/kernel/src/net/arp.c" \
  -o "$tmp/test_m5_arp"
"$tmp/test_m5_arp"

echo 'PASS: M5 ARP cache and resolution behavior'
