#!/usr/bin/env bash
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined \
  -fno-sanitize-recover=all -I"$root/kernel/include" \
  "$root/tests/host/test_m7c_ntp.c" "$root/user/ntp.c" \
  -o "$tmp/test_m7c_ntp"
ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 \
  UBSAN_OPTIONS=halt_on_error=1 "$tmp/test_m7c_ntp"

echo 'PASS: M7C NTP codec behavior'
