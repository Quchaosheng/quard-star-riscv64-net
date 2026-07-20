#!/usr/bin/env bash
set -eu

root=${QS_ROOT:-$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)}
export QS_ROOT=$root
export QS_STAGE=m6c2-stress
export QS_TEST_NAME=m6c2-stress
export QS_M6C2_STRESS=1
export QS_M6C2_TEST=1
export QS_M6C1_TEST=1
export QS_M6B_TEST=1

"$root/scripts/m6c2-build.sh"
QS_SMOKE_TIMEOUT=${QS_STRESS_TIMEOUT:-300} \
  exec "$root/scripts/m6c2-smoke.sh"
