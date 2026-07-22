#!/usr/bin/env bash
set -eu
root=${QS_ROOT:-$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)}
export QS_ROOT=$root QS_STAGE=m8 QS_TEST_NAME=m8-smoke
export QS_SMP=8 QS_REQUIRED_HARTS="0 1 2 3 4 5 6"
export QS_TRUSTED_SERIAL_LOG=$root/out/m8/trusted.log
export QS_EXTRA_MARKERS="QS:HART_ONLINE:0 QS:HART_ONLINE:1 QS:HART_ONLINE:2 QS:HART_ONLINE:3 QS:HART_ONLINE:4 QS:HART_ONLINE:5 QS:HART_ONLINE:6 QS:TEST_PASS:m8-smoke"
"$root/scripts/m5-smoke.sh"
grep -q 'QS:TRUSTED_READY' "$QS_TRUSTED_SERIAL_LOG"
grep -q 'QS:TRUSTED_SCHED_OK' "$QS_TRUSTED_SERIAL_LOG"
grep -q 'QS:PMP_UNTRUSTED_DENY_OK' "$root/out/m8/qemu.log"
grep -q 'QS:PMP_TRUSTED_DENY_OK' "$QS_TRUSTED_SERIAL_LOG"
grep -Eq 'Domain1 Region[0-9]+ +: 0x00000000bf800000-0x00000000bfffffff \(\)' \
  "$root/out/m8/qemu.log"
grep -Eq 'Domain2 Region[0-9]+ +: 0x00000000bf800000-0x00000000bfffffff \(R,W,X\)' \
  "$root/out/m8/qemu.log"
grep -Eq 'Domain1 Region[0-9]+ +: 0x0000000010002000-0x0000000010002fff \(I\)' \
  "$root/out/m8/qemu.log"
grep -Eq 'Domain2 Region[0-9]+ +: 0x0000000010002000-0x0000000010002fff \(I,R,W\)' \
  "$root/out/m8/qemu.log"
if grep -q 'QS:HART_ONLINE:7' "$root/out/m8/qemu.log" 2>/dev/null; then
  echo 'error: trusted hart7 entered the ordinary kernel' >&2
  exit 1
fi
