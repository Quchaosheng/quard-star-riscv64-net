#!/usr/bin/env bash
set -eu

root=${QS_ROOT:-$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)}
export QS_ROOT=$root
if [ -z "${QS_STAGE:-}" ]; then
  export QS_STAGE=m6c2
fi
if [ -z "${QS_TEST_NAME:-}" ]; then
  export QS_TEST_NAME=m6c2-smoke
fi
exec "$root/scripts/m6c1-smoke.sh"
