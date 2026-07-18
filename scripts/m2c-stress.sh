#!/usr/bin/env bash
set -eu

root=${QS_ROOT:-$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)}
export QS_ROOT=$root
export QS_STAGE=m2c-stress
export QS_SBI_DTS=$root/platform/quard-star/dts/quard_star_sbi_m2.dts
export QS_KERNEL_DTS=$root/platform/quard-star/dts/quard_star_kernel_m2.dts
export QS_KERNEL_CPPFLAGS="-DQS_M2C_TEST -DQS_M2C_STRESS -DQS_ALLOC_ITERATIONS=50000 -DQS_MIGRATION_TARGET=10000 -DQS_STRESS_MIN_TICKS=1200000000ULL"

"$root/scripts/m1-build.sh"
QS_TEST_NAME=m2c-stress QS_SMOKE_TIMEOUT=${QS_STRESS_TIMEOUT:-150} \
  exec "$root/scripts/m2c-smoke.sh"
