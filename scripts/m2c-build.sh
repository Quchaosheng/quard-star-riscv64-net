#!/usr/bin/env bash
set -eu

root=${QS_ROOT:-$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)}
export QS_ROOT=$root
export QS_STAGE=m2c
export QS_SBI_DTS=$root/platform/quard-star/dts/quard_star_sbi_m2.dts
export QS_KERNEL_DTS=$root/platform/quard-star/dts/quard_star_kernel_m2.dts
export QS_KERNEL_CPPFLAGS="-DQS_M2C_TEST -DQS_ALLOC_ITERATIONS=10000 -DQS_MIGRATION_TARGET=100"

exec "$root/scripts/m1-build.sh"
