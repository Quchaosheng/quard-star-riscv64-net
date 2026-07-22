#!/usr/bin/env bash
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
"$root/tests/host/run_m6c2_tcp_test.sh" \
  tests/host/test_m6c2_close.c \
  'M6C2 deferred TCP close'
