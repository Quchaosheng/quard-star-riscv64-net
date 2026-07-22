#!/usr/bin/env bash
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
"$root/tests/host/run_m6c2_tcp_test.sh" \
  tests/host/test_m6c2_pool.c \
  'M6C2 stable TCP PCB pool'
