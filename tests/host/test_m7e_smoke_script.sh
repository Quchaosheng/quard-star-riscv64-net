#!/usr/bin/env bash
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
smoke=$root/scripts/m5-smoke.sh
grep -Fq 'm7e-smoke' "$smoke"
grep -Fq -- '--require-tftp-1m' "$smoke"
for marker in QS:M7E_TFTP_RRQ_OK QS:M7E_TFTP_1M_OK QS:M7E_TFTP_SHA256_OK \
  QS:M7E_TFTP_REOPEN_OK QS:M7E_TFTP_TIMEOUT_OK; do
  grep -Fq "$marker" "$smoke"
done
grep -Fq 'data.get("tftp_bytes") == 1048576' "$smoke"
echo 'PASS: M7E smoke script contracts'
