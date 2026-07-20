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

for text in \
  'QS_ALLOC_ITERATIONS=50000' \
  'QS_MIGRATION_TARGET=10000' \
  'QS_STRESS_MIN_TICKS=1200000000ULL'; do
  grep -Fq "$text" "$root/scripts/m6b-build.sh" || \
    fail "missing cumulative stress build setting $text"
done

poll=$(awk '
  /^void m2c_selftest_poll\(void\)$/ { body = 1 }
  body { print }
  body && /^}/ { exit }
' "$source_file")
printf '%s\n' "$poll" | grep -Fq 'm6c2_stress_ready()' || \
  fail 'final poll bypasses stress counter gate'

for text in \
  '#define STRESS_PARALLEL 8' \
  '#define STRESS_RECONNECTS 100' \
  'run_stress_server(listener)' \
  'wait_peer_close'; do
  grep -Fq "$text" "$root/user/tcp_server_echo.c" || \
    fail "missing guest stress behavior $text"
done

echo 'PASS: M6C2 stress evidence contracts'
