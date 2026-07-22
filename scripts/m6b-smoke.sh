#!/usr/bin/env bash
set -eu

root=${QS_ROOT:-$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)}
export QS_ROOT=$root
if [ -z "${QS_STAGE:-}" ]; then
  QS_STAGE=m6b
fi
if [ -z "${QS_TEST_NAME:-}" ]; then
  QS_TEST_NAME=m6b-smoke
fi
export QS_STAGE QS_TEST_NAME
exec "$root/scripts/m6a-smoke.sh"
