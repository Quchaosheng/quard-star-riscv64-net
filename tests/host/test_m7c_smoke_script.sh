#!/usr/bin/env bash
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
smoke=$root/scripts/m5-smoke.sh
peer=$root/scripts/m5-peer.py

grep -Fq '[ "$test_name" = m7c-smoke ]' "$smoke"
grep -Fq -- '--require-ntp' "$smoke"
for marker in QS:M7C_NTP_QUERY_OK QS:M7C_NTP_RESPONSE_OK \
  QS:M7C_NTP_TIMEOUT_OK; do
  grep -Fq "$marker" "$smoke"
done
for key in ntp_queries ntp_replies ntp_timeouts; do
  grep -Fq "$key" "$smoke"
  grep -Fq "$key" "$peer"
done

echo 'PASS: M7C NTP smoke script behavior'
