#!/usr/bin/env bash
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
"$root/tests/host/run_m6c2_tcp_test.sh" \
  tests/host/test_m6c2_socket.c \
  'M6C2 passive socket lifetime' \
  '-DSOCKET_TEST'
