#!/usr/bin/env bash
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined \
  -fno-sanitize-recover=undefined \
  -pthread -I"$root/kernel/include" -I"$root/kernel/include/timeros/net" \
  "$root/tests/host/test_m6_runtime.c" \
  "$root/kernel/src/net/net_sys.c" \
  -o "$tmp/test_m6_runtime"
"$tmp/test_m6_runtime"

echo 'PASS: M6 network runtime time and semaphore behavior'
