#!/usr/bin/env bash
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
grep -Fq 'possible-harts = <&cpu0 &cpu1 &cpu2 &cpu3 &cpu4 &cpu5 &cpu6>;' \
  "$root/platform/quard-star/dts/quard_star_sbi_m8.dts"
grep -Fq 'possible-harts = <&cpu7>;' \
  "$root/platform/quard-star/dts/quard_star_sbi_m8.dts"
grep -Fq 'next-addr = <0 0xbf800000>;' \
  "$root/platform/quard-star/dts/quard_star_sbi_m8.dts"
for hart in 1 2 3 4 5 6; do
  grep -Fq "cpu$hart: cpu@$hart" \
    "$root/platform/quard-star/dts/quard_star_kernel_m8.dts"
done
grep -Fq -- '-smp 8' "$root/scripts/m8-smoke.sh"
grep -Fq 'QS:TRUSTED_READY' "$root/scripts/m8-smoke.sh"
grep -Fq 'QS:HART_ONLINE:7' "$root/scripts/m8-smoke.sh"
grep -Fq 'QS:TEST_PASS:m8-smoke' "$root/kernel/src/selftest.c"
echo 'PASS: M8 trusted-domain and seven-hart contracts'
