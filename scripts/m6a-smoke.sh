#!/usr/bin/env bash
set -eu

script_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
root=${QS_ROOT:-$script_root}
export QS_ROOT=$root
if [ -z "${QS_STAGE:-}" ]; then
  QS_STAGE=m6a
fi
if [ -z "${QS_TEST_NAME:-}" ]; then
  QS_TEST_NAME=m6a-smoke
fi
export QS_STAGE QS_TEST_NAME
exec "$script_root/scripts/m5-smoke.sh"
