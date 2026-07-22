#!/usr/bin/env bash
set -eu

root=${QS_ROOT:-$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)}
export QS_ROOT=$root
export QS_STAGE=m7a
export QS_TEST_NAME=m7a-smoke
export QS_M7A_TEST=1
exec "$root/scripts/m5-smoke.sh"
