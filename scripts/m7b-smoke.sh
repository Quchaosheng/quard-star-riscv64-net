#!/usr/bin/env bash
set -eu

root=${QS_ROOT:-$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)}
export QS_ROOT=$root
export QS_STAGE=m7b
export QS_TEST_NAME=m7b-smoke
export QS_M7A_TEST=1
export QS_M7B_TEST=1
exec "$root/scripts/m5-smoke.sh"
