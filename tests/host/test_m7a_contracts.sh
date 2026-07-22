#!/usr/bin/env bash
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
grep -Fq 'sys_dns_resolve' "$root/user/m7a_dns_echo.c"
grep -Fq 'QS:M7A_DNS_QUERY_OK' "$root/user/m7a_dns_echo.c"
grep -Fq 'QS:M7A_DNS_RESOLVE_OK' "$root/user/m7a_dns_echo.c"
grep -Fq 'QS:M7A_DNS_TIMEOUT_OK' "$root/user/m7a_dns_echo.c"
grep -Fq 'QS:TEST_PASS:m7a-smoke' "$root/kernel/src/selftest.c"
grep -Fq 'QS_M7A_TEST' "$root/scripts/m6b-build.sh"
grep -Fq 'm7a_dns_echo' "$root/user/Makefile"
grep -Fq 'm7a-smoke' "$root/scripts/m5-smoke.sh"

echo 'PASS: M7A DNS build and guest contracts'
