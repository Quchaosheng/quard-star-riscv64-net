#!/usr/bin/env bash
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
source_file=$1
label=$2
extra_flags=${3:-}
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

# extra_flags is an optional caller-supplied list of compiler arguments.
# shellcheck disable=SC2086
cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined \
  -fno-sanitize-recover=all -pthread $extra_flags \
  -I"$root/kernel/include" -I"$root/kernel/include/timeros/net" \
  "$root/$source_file" \
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
  -o "$tmp/test_m6c2_tcp"
ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 \
  UBSAN_OPTIONS=halt_on_error=1 "$tmp/test_m6c2_tcp"

echo "PASS: $label"
