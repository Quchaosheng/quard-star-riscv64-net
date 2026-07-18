#!/usr/bin/env bash
set -eu

root=${QS_ROOT:-$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)}
"$root/scripts/prepare-fatfs.sh"

export QS_ROOT=$root
export QS_STAGE=m4
export QS_SBI_DTS=$root/platform/quard-star/dts/quard_star_sbi_m2.dts
export QS_KERNEL_DTS=$root/platform/quard-star/dts/quard_star_kernel_m2.dts
export QS_KERNEL_CPPFLAGS="-DQS_M2C_TEST -DQS_M3_TEST -DQS_M4_TEST -DQS_ALLOC_ITERATIONS=10000 -DQS_MIGRATION_TARGET=100 -DQS_FATFS_ITERATIONS=4 -DQS_NET_ITERATIONS=32 -DQS_NET_RESETS=1"
export QS_KERNEL_FATFS=1

exec "$root/scripts/m1-build.sh"
