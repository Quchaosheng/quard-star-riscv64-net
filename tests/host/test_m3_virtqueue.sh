#!/usr/bin/env bash
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined \
  -I"$root/kernel/include" \
  "$root/tests/host/test_virtqueue.c" "$root/kernel/src/virtqueue.c" \
  -o "$tmp/test_virtqueue"
"$tmp/test_virtqueue"

echo "PASS: M3 virtqueue behavior"
