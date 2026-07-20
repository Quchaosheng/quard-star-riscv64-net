#!/usr/bin/env bash
set -eu

root=${QS_ROOT:-$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)}
export QS_ROOT=$root
export QS_STAGE=m6c2
export QS_TEST_NAME=m6c2-smoke
exec "$root/scripts/m6c1-smoke.sh"
