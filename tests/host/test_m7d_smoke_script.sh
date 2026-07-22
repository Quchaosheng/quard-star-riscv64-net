#!/usr/bin/env bash
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
smoke=$root/scripts/m5-smoke.sh
peer=$root/scripts/m5-peer.py

grep -Fq '[ "$test_name" = m7d-smoke ]' "$smoke"
grep -Fq -- '--require-tftp' "$smoke"
for marker in QS:M7D_TFTP_RRQ_OK QS:M7D_TFTP_DATA1_OK \
  QS:M7D_TFTP_DATA2_OK QS:M7D_TFTP_CHECKSUM_OK QS:M7D_TFTP_TIMEOUT_OK; do
  grep -Fq "$marker" "$smoke"
done
for key in tftp_rrq tftp_data tftp_acks tftp_timeouts tftp_outstanding; do
  grep -Fq "$key" "$smoke"
  grep -Fq "$key" "$peer"
done

echo 'PASS: M7D TFTP smoke script behavior'
