#!/usr/bin/env bash
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined \
  -I"$root/kernel/include" -I"$root/kernel/include/timeros/net" \
  "$root/tests/host/test_m6_timer.c" \
  "$root/kernel/src/net/nlist.c" \
  "$root/kernel/src/net/timer.c" \
  -o "$tmp/test_m6_timer"
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 \
  "$tmp/test_m6_timer"

echo 'PASS: M6 shared network timer behavior'
