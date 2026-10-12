#!/usr/bin/env bash
set -eu
root=${QS_ROOT:-$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)}
# The identity of the build under test travels with the run, so a cached run
# cannot be mistaken for current-source evidence.
export QS_M8_BUILD_IDENTITY=${QS_M8_BUILD_IDENTITY:-$(git -C "$root" rev-parse --short=12 HEAD 2>/dev/null || echo unknown)}
echo "QS:M8_BUILD_IDENTITY:$QS_M8_BUILD_IDENTITY"
export QS_ROOT=$root QS_STAGE=m8 QS_TEST_NAME=m8-smoke
export QS_SMP=8 QS_REQUIRED_HARTS="0 1 2 3 4 5 6"
export QS_TRUSTED_SERIAL_LOG=$root/out/m8/trusted.log
export QS_EXTRA_MARKERS="QS:HART_ONLINE:0 QS:HART_ONLINE:1 QS:HART_ONLINE:2 QS:HART_ONLINE:3 QS:HART_ONLINE:4 QS:HART_ONLINE:5 QS:HART_ONLINE:6 QS:TEST_PASS:m8-smoke"
"$root/scripts/m5-smoke.sh"

# Each M8-only marker must be present and reported by name; a bare `grep -q`
# under `set -e` would exit 1 with no diagnostic when a runtime property is
# missing or a domain was misconfigured.
require_line() {
  if ! tr -d '\000\r' < "$2" | grep -Fqx "$1"; then
    echo "error: missing $1 in $2" >&2
    exit 1
  fi
}
require_match() {
  if ! grep -Eq "$1" "$2"; then
    echo "error: missing /$1/ in $2" >&2
    exit 1
  fi
}

require_line 'QS:TRUSTED_READY' "$QS_TRUSTED_SERIAL_LOG"
require_line 'QS:TRUSTED_SCHED_OK' "$QS_TRUSTED_SERIAL_LOG"
require_line 'QS:PMP_UNTRUSTED_DENY_OK' "$root/out/m8/qemu.log"
for access in LOAD STORE EXEC; do
  require_line "QS:PMP_UNTRUSTED_${access}_DENY_OK" "$root/out/m8/qemu.log"
done
require_line 'QS:PMP_TRUSTED_DENY_OK' "$QS_TRUSTED_SERIAL_LOG"
for access in LOAD STORE EXEC; do
  require_line "QS:PMP_TRUSTED_${access}_DENY_OK" "$QS_TRUSTED_SERIAL_LOG"
done
require_match 'Domain1 Region[0-9]+ +: 0x00000000bf800000-0x00000000bfffffff \(\)' \
  "$root/out/m8/qemu.log"
require_match 'Domain2 Region[0-9]+ +: 0x00000000bf800000-0x00000000bfffffff \(R,W,X\)' \
  "$root/out/m8/qemu.log"
require_match 'Domain1 Region[0-9]+ +: 0x0000000010002000-0x0000000010002fff \(I\)' \
  "$root/out/m8/qemu.log"
require_match 'Domain2 Region[0-9]+ +: 0x0000000010002000-0x0000000010002fff \(I,R,W\)' \
  "$root/out/m8/qemu.log"
if grep -q 'QS:HART_ONLINE:7' "$root/out/m8/qemu.log" 2>/dev/null; then
  echo 'error: trusted hart7 entered the ordinary kernel' >&2
  exit 1
fi
