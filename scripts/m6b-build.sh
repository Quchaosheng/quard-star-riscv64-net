#!/usr/bin/env bash
set -eu

root=${QS_ROOT:-$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)}
export QS_ROOT=$root
export QS_STAGE=m6b
export QS_KERNEL_CPPFLAGS="-DQS_M2C_TEST -DQS_M3_TEST -DQS_M4_TEST -DQS_M5_TEST -DQS_M6A_TEST -DQS_M6B_TEST -DQS_ALLOC_ITERATIONS=10000 -DQS_MIGRATION_TARGET=100 -DQS_FATFS_ITERATIONS=4 -DQS_NET_ITERATIONS=32 -DQS_NET_RESETS=1"
exec "$root/scripts/m6a-build.sh"
