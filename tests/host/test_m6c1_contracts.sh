#!/usr/bin/env bash
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
fail() { echo "FAIL: $*" >&2; exit 1; }

grep -q '^m6c1-build:' "$root/Makefile" || fail 'missing m6c1-build target'
grep -q '^m6c1-smoke:' "$root/Makefile" || fail 'missing m6c1-smoke target'
grep -q 'QS_M6C1_TEST' "$root/scripts/m6c1-build.sh" || fail 'missing M6C1 flag'
grep -q 'm6b-build.sh' "$root/scripts/m6c1-build.sh" || fail 'M6C1 must reuse M6B build'
grep -q 'm6b-smoke.sh' "$root/scripts/m6c1-smoke.sh" || fail 'M6C1 must reuse M6B smoke'
grep -q 'NET_PROTOCOL_TCP' "$root/kernel/src/net/tcp.c" 2>/dev/null || \
  fail 'missing TCP protocol implementation'
grep -q 'tcp_echo' "$root/user/Makefile" || fail 'missing TCP guest program'

echo 'PASS: M6C1 TCP build contracts'
