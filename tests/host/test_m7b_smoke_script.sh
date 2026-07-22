#!/usr/bin/env bash
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
smoke=$root/scripts/m5-smoke.sh
peer=$root/scripts/m5-peer.py

grep -Fq '[ "$test_name" = m7b-smoke ]' "$smoke"
grep -Fq -- '--require-http' "$smoke"
for marker in QS:M7B_HTTP_DNS_OK QS:M7B_HTTP_CONNECT_OK \
  QS:M7B_HTTP_RESPONSE_OK QS:M7B_HTTP_CLOSE_OK; do
  grep -Fq "$marker" "$smoke"
done
for key in http_requests http_responses http_outstanding; do
  grep -Fq "$key" "$smoke"
  grep -Fq "$key" "$peer"
done

echo 'PASS: M7B HTTP smoke script behavior'
