#!/usr/bin/env bash
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
source_file=$root/kernel/src/selftest.c

fail()
{
  echo "FAIL: $*" >&2
  exit 1
}

for text in \
  '#define M6C2_STRESS_CONNECTIONS 108U' \
  '#define M6C2_STRESS_PARALLEL 8U' \
  'm6c2_stress_accepted' \
  'm6c2_stress_echoed' \
  'm6c2_stress_live' \
  'm6c2_stress_peak' \
  'm6c2_stress_released' \
  'QS:M6C2_STRESS_PARALLEL_OK' \
  'QS:M6C2_STRESS_RECONNECT_OK' \
  'QS:TEST_PASS:m6c2-stress'; do
  grep -Fq "$text" "$source_file" || fail "missing $text"
done

poll=$(awk '
  /^void m2c_selftest_poll\(void\)$/ { body = 1 }
  body { print }
  body && /^}/ { exit }
' "$source_file")
printf '%s\n' "$poll" | grep -Fq 'm6c2_stress_ready()' || \
  fail 'final poll bypasses stress counter gate'

echo 'PASS: M6C2 stress evidence contracts'
