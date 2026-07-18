#!/usr/bin/env bash
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined \
  -I"$root/kernel/include" \
  "$root/tests/host/test_m4_completion.c" \
  "$root/kernel/src/virtio_net_completion.c" \
  -o "$tmp/test_m4_completion"
"$tmp/test_m4_completion"

echo "PASS: M4 completion ring behavior"
