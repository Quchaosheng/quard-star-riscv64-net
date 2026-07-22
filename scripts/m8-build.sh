#!/usr/bin/env bash
set -eu
root=${QS_ROOT:-$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)}
export QS_ROOT=$root QS_STAGE=m8
export QS_SBI_DTS=$root/platform/quard-star/dts/quard_star_sbi_m8.dts
export QS_KERNEL_DTS=$root/platform/quard-star/dts/quard_star_kernel_m8.dts
export QS_KERNEL_CPPFLAGS="-DQS_M2C_TEST -DQS_M3_TEST -DQS_M4_TEST -DQS_M5_TEST -DQS_M6A_TEST -DQS_M6B_TEST -DQS_M7A_TEST -DQS_M7B_TEST -DQS_M7C_TEST -DQS_M7D_TEST -DQS_M7E_TEST -DQS_M8_TEST -DQS_ALLOC_ITERATIONS=2000 -DQS_MIGRATION_TARGET=100 -DQS_FATFS_ITERATIONS=4 -DQS_NET_ITERATIONS=32 -DQS_NET_RESETS=1 -DNET_STACK_RX_WAIT_MS=1"
export QS_KERNEL_FATFS=1
rm -f "$root/out/m8/disk/disk.img"
exec "$root/scripts/m1-build.sh"
