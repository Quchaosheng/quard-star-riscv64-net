#!/usr/bin/env bash
set -eu
root=${QS_ROOT:-$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)}
export QS_ROOT=$root QS_STAGE=m7e QS_TEST_NAME=m7e-smoke QS_M7E_TEST=1
exec "$root/scripts/m5-smoke.sh"
