#!/usr/bin/env bash
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined \
  -fno-sanitize-recover=all -I"$root/kernel/include" \
  "$root/tests/host/test_m7d_tftp.c" "$root/user/tftp.c" \
  -o "$tmp/test_m7d_tftp"
ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 \
  UBSAN_OPTIONS=halt_on_error=1 "$tmp/test_m7d_tftp"

echo 'PASS: M7D TFTP codec behavior'
