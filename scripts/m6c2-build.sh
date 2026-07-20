#!/usr/bin/env bash
set -eu

root=${QS_ROOT:-$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)}
export QS_ROOT=$root
export QS_STAGE=m6c2
export QS_M6C2_TEST=1
export QS_M6C1_TEST=1
export QS_M6B_TEST=1
exec "$root/scripts/m6c1-build.sh"
