#!/usr/bin/env bash
set -eu
root=${QS_ROOT:-$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)}
export QS_ROOT=$root QS_STAGE=m8
export QS_SBI_DTS=$root/platform/quard-star/dts/quard_star_sbi_m8.dts
export QS_KERNEL_DTS=$root/platform/quard-star/dts/quard_star_kernel_m8.dts
export QS_KERNEL_CPPFLAGS="-DQS_M2C_TEST -DQS_M8_TEST -DQS_ALLOC_ITERATIONS=2000 -DQS_MIGRATION_TARGET=100"
export QS_KERNEL_FATFS=1
exec "$root/scripts/m1-build.sh"
