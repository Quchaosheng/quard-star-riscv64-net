#!/usr/bin/env bash
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
status=0

grep -Fq 'dns_resolve_a(' "$root/kernel/include/timeros/net/dns.h" || status=1
grep -Fq 'net_exec_submit' "$root/kernel/src/net/dns_resolver.c" || status=1
grep -Fq '#define __NR_dns_resolve' "$root/kernel/include/timeros/syscall.h" || status=1
grep -Fq 'case __NR_dns_resolve:' "$root/kernel/src/syscall.c" || status=1

if [ "$status" -ne 0 ]; then
  echo 'FAIL: M7A DNS resolver integration contract'
  exit 1
fi

echo 'PASS: M7A DNS resolver integration contract'
