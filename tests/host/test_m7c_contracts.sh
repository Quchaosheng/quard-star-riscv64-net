#!/usr/bin/env bash
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
grep -Fq 'm7c_ntp_get' "$root/user/Makefile"
grep -Fq 'QS_M7C_TEST' "$root/scripts/m6b-build.sh"
grep -Fq 'QS:M7C_NTP_QUERY_OK' "$root/user/m7c_ntp_get.c"
grep -Fq 'QS:M7C_NTP_RESPONSE_OK' "$root/user/m7c_ntp_get.c"
grep -Fq 'QS:M7C_NTP_TIMEOUT_OK' "$root/user/m7c_ntp_get.c"
grep -Fq 'QS:TEST_PASS:m7c-smoke' "$root/kernel/src/selftest.c"
grep -Fq -- '--require-ntp' "$root/scripts/m5-peer.py"

echo 'PASS: M7C NTP build and guest contracts'
