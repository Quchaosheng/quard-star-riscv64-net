#!/usr/bin/env bash
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
fail() { echo "FAIL: $*" >&2; exit 1; }

grep -q '^m6b-build:' "$root/Makefile" || fail 'missing m6b-build target'
grep -q '^m6b-smoke:' "$root/Makefile" || fail 'missing m6b-smoke target'
grep -q 'QS_M6B_TEST' "$root/scripts/m6b-build.sh" || fail 'missing M6B flag'
grep -q 'm6a-build.sh' "$root/scripts/m6b-build.sh" || fail 'M6B must reuse M6A build'
grep -q 'sys_socket' "$root/user/udp_echo.c" || fail 'guest must use socket syscall'
grep -q 'sys_sendto' "$root/user/udp_echo.c" || fail 'guest must send UDP'
grep -q 'sys_recvfrom' "$root/user/udp_echo.c" || fail 'guest must receive UDP'
grep -q 'QS:M6B_UDP_OK' "$root/user/udp_echo.c" || fail 'missing UDP marker'
grep -q 'QS:M6B_UDP_TIMEOUT_OK' "$root/user/udp_echo.c" || fail 'missing timeout marker'
grep -q 'TEST_SOCKET_COUNT 16' "$root/user/udp_echo.c" || \
  fail 'guest must exercise all 16 target socket slots'
grep -q 'for (int i = 1; i < TEST_SOCKET_COUNT; i++)' \
  "$root/user/udp_echo.c" || fail 'guest must close the tested socket last'
grep -q 'retired' "$root/kernel/src/net/socket.c" || \
  fail 'exhausted generations must retire instead of wrapping'
grep -q 'QS:TEST_PASS:m6b-smoke' "$root/kernel/src/selftest.c" || \
  fail 'missing M6B pass marker'
grep -q 'sys_write(stdout,out_buf,res);' "$root/kernel/lib/printf.c" || \
  fail 'user printf must not write the string terminator'
grep -q 'TAP ARP/ICMP/UDP acceptance' "$root/scripts/m5-smoke.sh" || \
  fail 'M6B result must describe UDP acceptance'
sem_max=$(sed -n 's/^#define NET_SYS_SEM_MAX //p' "$root/kernel/src/net/net_sys.c")
[ "${sem_max:-0}" -ge 68 ] || \
  fail 'target semaphore pool must support all 16 UDP sockets'

echo 'PASS: M6B source and build contracts'
