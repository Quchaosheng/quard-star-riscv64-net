#!/usr/bin/env bash
set -eu

root=${QS_ROOT:-$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)}
"$root/scripts/prepare-fatfs.sh"

export QS_ROOT=$root
export QS_STAGE=m3-stress
export QS_SBI_DTS=$root/platform/quard-star/dts/quard_star_sbi_m2.dts
export QS_KERNEL_DTS=$root/platform/quard-star/dts/quard_star_kernel_m2.dts
export QS_KERNEL_CPPFLAGS="-DQS_M2C_TEST -DQS_M3_TEST -DQS_M3_STRESS -DQS_ALLOC_ITERATIONS=50000 -DQS_MIGRATION_TARGET=10000 -DQS_FATFS_ITERATIONS=128 -DQS_STRESS_MIN_TICKS=1200000000ULL"
export QS_KERNEL_FATFS=1

"$root/scripts/m1-build.sh"
QS_EXTRA_MARKERS='QS:VIRTQUEUE_OK QS:BLOCK_IRQ_OK QS:BLOCK_STRESS_OK QS:FATFS_OK QS:FATFS_ITERATIONS:128' \
QS_TEST_NAME=m3-stress QS_SMOKE_TIMEOUT=${QS_STRESS_TIMEOUT:-180} \
  exec "$root/scripts/m2c-smoke.sh"
