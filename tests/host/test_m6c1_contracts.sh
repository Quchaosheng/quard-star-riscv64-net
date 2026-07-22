#!/usr/bin/env bash
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
fail() { echo "FAIL: $*" >&2; exit 1; }

grep -q '^m6c1-build:' "$root/Makefile" || fail 'missing m6c1-build target'
grep -q '^m6c1-smoke:' "$root/Makefile" || fail 'missing m6c1-smoke target'
grep -qx 'export QS_ROOT=$root' "$root/scripts/m6c1-build.sh" || \
  fail 'M6C1 build must export QS_ROOT'
grep -qx 'export QS_M6C1_TEST=1' "$root/scripts/m6c1-build.sh" || \
  fail 'missing M6C1 flag'
grep -qx 'export QS_M6B_TEST=1' "$root/scripts/m6c1-build.sh" || \
  fail 'missing M6B flag'
grep -q 'm6b-build.sh' "$root/scripts/m6c1-build.sh" || fail 'M6C1 must reuse M6B build'
grep -qx 'export QS_ROOT=$root' "$root/scripts/m6c1-smoke.sh" || \
  fail 'M6C1 smoke must export QS_ROOT'
grep -qx 'export QS_STAGE=m6c1' "$root/scripts/m6c1-smoke.sh" || \
  fail 'missing M6C1 stage'
grep -qx 'export QS_TEST_NAME=m6c1-smoke' "$root/scripts/m6c1-smoke.sh" || \
  fail 'missing M6C1 test name'
grep -q 'm6b-smoke.sh' "$root/scripts/m6c1-smoke.sh" || fail 'M6C1 must reuse M6B smoke'
grep -q 'NET_PROTOCOL_TCP' "$root/kernel/src/net/tcp.c" 2>/dev/null || \
  fail 'missing TCP protocol implementation'
for name in connect send recv; do
  grep -q "^#define __NR_$name " "$root/kernel/include/timeros/syscall.h" || \
    fail "missing __NR_$name"
  grep -q "case __NR_$name:" "$root/kernel/src/syscall.c" || \
    fail "missing dispatch for $name"
  grep -q "sys_$name" "$root/kernel/lib/app.c" || \
    fail "missing user $name wrapper"
done
grep -q '^#define NET_SOCK_STREAM 1' "$root/kernel/include/timeros/syscall.h" || \
  fail 'missing stream socket ABI type'
grep -q 'entry->type != NET_SOCKET_TCP' "$root/kernel/src/net/socket.c" || \
  fail 'TCP socket operations must reject non-TCP handles'
awk '
  /static int __sys_recv\(/ { in_recv = 1 }
  in_recv && /user_range_check\(args.data/ { checked = 1 }
  in_recv && /net_socket_recv\(handle/ {
    if (!checked) exit 1
    found = 1
    exit 0
  }
  END { if (!found) exit 1 }
' "$root/kernel/src/syscall.c" || \
  fail 'recv must validate writable output before consuming the stream'
if [ -f "$root/user/tcp_echo.c" ]; then
  grep -q 'tcp_echo' "$root/user/Makefile" || fail 'missing TCP guest program'
fi

echo 'PASS: M6C1 TCP build contracts'
