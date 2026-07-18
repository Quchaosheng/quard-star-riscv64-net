#!/usr/bin/env bash
set -eu

script_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
root=${QS_ROOT:-$script_root}
export QS_ROOT=$root
export QS_STAGE=m6a
export QS_TEST_NAME=m6a-smoke
exec "$script_root/scripts/m5-smoke.sh"
