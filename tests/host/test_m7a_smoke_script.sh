#!/usr/bin/env bash
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
smoke=$root/scripts/m5-smoke.sh
peer=$root/scripts/m5-peer.py

grep -Fq '[ "$test_name" = m7a-smoke ]' "$smoke"
grep -Fq -- '--require-dns' "$smoke"
grep -Fq 'QS:M7A_DNS_QUERY_OK' "$smoke"
grep -Fq 'QS:M7A_DNS_RESOLVE_OK' "$smoke"
grep -Fq 'QS:M7A_DNS_TIMEOUT_OK' "$smoke"
grep -Fq 'dns_queries' "$smoke"
grep -Fq 'dns_replies' "$smoke"
grep -Fq 'dns_timeouts' "$smoke"
grep -Fq -- '--require-dns' "$peer"
grep -Fq 'dns_response' "$peer"

echo 'PASS: M7A DNS smoke script behavior'
