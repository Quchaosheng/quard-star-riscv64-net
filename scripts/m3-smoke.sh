#!/usr/bin/env bash
set -eu

script_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
root=${QS_ROOT:-$script_root}
export QS_ROOT=$root
export QS_STAGE=m3
export QS_TEST_NAME=m3-smoke
export QS_EXTRA_MARKERS='QS:VIRTQUEUE_OK QS:BLOCK_IRQ_OK QS:BLOCK_STRESS_OK QS:FATFS_OK QS:TEST_PASS:m3-smoke'
export QS_SMOKE_TIMEOUT=${QS_SMOKE_TIMEOUT:-60}

exec "$script_root/scripts/m2c-smoke.sh"
