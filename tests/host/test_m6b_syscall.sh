#!/usr/bin/env bash
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)

fail()
{
  echo "FAIL: $*" >&2
  exit 1
}

for name in socket bind sendto recvfrom close; do
  grep -q "__NR_$name" "$root/kernel/include/timeros/syscall.h" || \
    fail "missing __NR_$name"
  grep -q "case __NR_$name:" "$root/kernel/src/syscall.c" || \
    fail "missing dispatch for $name"
done
grep -q 'net_socket_open' "$root/kernel/src/syscall.c" || \
  fail 'socket syscall must use the bounded socket table'
grep -q 'net_exec_submit' "$root/kernel/src/syscall.c" || \
  fail 'socket mutations must enter the network executor'
grep -q 'timeros/net/tools.h' "$root/kernel/src/syscall.c" || \
  fail 'socket ABI byte-order helpers must be declared'
grep -q 'typedef struct.*net_sockaddr_in' "$root/kernel/include/timeros/syscall.h" || \
  fail 'missing fixed-width socket address ABI'
grep -q 'sys_sendto' "$root/kernel/lib/app.c" || \
  fail 'missing user sendto wrapper'
grep -q 'sys_recvfrom' "$root/kernel/lib/app.c" || \
  fail 'missing user recvfrom wrapper'
grep -q 'user_range_check.*args.data' "$root/kernel/src/syscall.c" || \
  fail 'recvfrom must validate output before consuming a datagram'
grep -q 'm6b_timeout_observed' "$root/kernel/src/syscall.c" || \
  fail 'kernel must defer timeout completion until close'

echo 'PASS: M6B socket syscall contracts'
