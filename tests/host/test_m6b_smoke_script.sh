#!/usr/bin/env bash
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)

grep -q 'm6a-smoke.sh' "$root/scripts/m6b-smoke.sh"
for marker in QS:M6B_UDP_OK QS:M6B_UDP_TIMEOUT_OK; do
  grep -q "$marker" "$root/scripts/m5-smoke.sh"
done
grep -q 'QS:TEST_PASS:\$test_name' "$root/scripts/m5-smoke.sh"
grep -q 'udp_' "$root/scripts/m5-peer.py"

echo 'PASS: M6B smoke script contracts'
