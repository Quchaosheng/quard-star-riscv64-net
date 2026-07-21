#!/usr/bin/env bash
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
grep -Fq 'm7b_http_get' "$root/user/Makefile"
grep -Fq 'QS_M7B_TEST' "$root/scripts/m6b-build.sh"
grep -Fq 'QS:M7B_HTTP_DNS_OK' "$root/user/m7b_http_get.c"
grep -Fq 'QS:M7B_HTTP_CONNECT_OK' "$root/user/m7b_http_get.c"
grep -Fq 'QS:M7B_HTTP_RESPONSE_OK' "$root/user/m7b_http_get.c"
grep -Fq 'QS:M7B_HTTP_CLOSE_OK' "$root/user/m7b_http_get.c"
grep -Fq 'QS:TEST_PASS:m7b-smoke' "$root/kernel/src/selftest.c"
grep -Fq -- '--require-http' "$root/scripts/m5-peer.py"
grep -Fq 'm7b-smoke' "$root/scripts/m5-smoke.sh"

echo 'PASS: M7B HTTP build and guest contracts'
