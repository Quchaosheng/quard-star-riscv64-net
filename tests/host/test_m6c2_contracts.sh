#!/usr/bin/env bash
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
fail() { echo "FAIL: $*" >&2; exit 1; }

grep -q '^m6c2-build:' "$root/Makefile" || fail 'missing m6c2-build target'
grep -q '^m6c2-smoke:' "$root/Makefile" || fail 'missing m6c2-smoke target'
grep -qx 'export QS_M6C2_TEST=1' "$root/scripts/m6c2-build.sh" || \
  fail 'missing M6C2 build flag'
grep -q 'm6c1-build.sh' "$root/scripts/m6c2-build.sh" || \
  fail 'M6C2 build must reuse M6C1'
grep -qx 'export QS_STAGE=m6c2' "$root/scripts/m6c2-smoke.sh" || \
  fail 'missing M6C2 smoke stage'
grep -qx 'export QS_TEST_NAME=m6c2-smoke' "$root/scripts/m6c2-smoke.sh" || \
  fail 'missing M6C2 smoke name'
grep -q 'm6c1-smoke.sh' "$root/scripts/m6c2-smoke.sh" || \
  fail 'M6C2 smoke must reuse M6C1'
for name in listen accept; do
  grep -q "^#define __NR_$name " "$root/kernel/include/timeros/syscall.h" || \
    fail "missing __NR_$name"
  grep -q "case __NR_$name:" "$root/kernel/src/syscall.c" || \
    fail "missing $name dispatch"
  grep -q "sys_$name" "$root/kernel/lib/app.c" || \
    fail "missing user $name wrapper"
done
grep -q 'TCP_STATE_LISTEN' "$root/kernel/include/timeros/net/tcp.h" || \
  fail 'missing LISTEN state'
grep -q 'TCP_STATE_SYN_RECEIVED' "$root/kernel/include/timeros/net/tcp.h" || \
  fail 'missing SYN_RECEIVED state'
grep -q 'tcp_server_echo' "$root/user/Makefile" || \
  fail 'missing server Echo target'

echo 'PASS: M6C2 passive TCP contracts'
