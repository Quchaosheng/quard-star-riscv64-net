#!/usr/bin/env bash
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
status=0

for generated in \
  kernel/build.out \
  kernel/src/link_app.S \
  trusted/build/obj/main.d \
  user/bin/initproc
do
  if ! git -C "$root" check-ignore -q "$generated"; then
    echo "FAIL: generated path is not ignored: $generated" >&2
    status=1
  fi
done

mkdir -p "$root/user/bin"
touch -d '2030-01-01 00:00:00 UTC' "$root/user/bin"
if ! make -C "$root/user" all >"$tmp/user-build.out" 2>"$tmp/user-build.err"; then
  cat "$tmp/user-build.out" >&2
  cat "$tmp/user-build.err" >&2
  echo "FAIL: user programs must build" >&2
  status=1
fi
touch "$root/user/bin"
if grep -q 'Clock skew detected' "$tmp/user-build.out" "$tmp/user-build.err"; then
  cat "$tmp/user-build.out" >&2
  cat "$tmp/user-build.err" >&2
  echo "FAIL: user build must not depend on directory timestamps" >&2
  status=1
fi

if ! make -C "$root/kernel" clean all >"$tmp/kernel-build.out" 2>"$tmp/kernel-build.err"; then
  cat "$tmp/kernel-build.out" >&2
  cat "$tmp/kernel-build.err" >&2
  echo "FAIL: kernel must build" >&2
  status=1
fi
if grep -q 'Clock skew detected' "$tmp/kernel-build.out" "$tmp/kernel-build.err"; then
  cat "$tmp/kernel-build.out" >&2
  cat "$tmp/kernel-build.err" >&2
  echo "FAIL: kernel build must not depend on generated-file timestamps" >&2
  status=1
fi
if ! grep -Fq "touch -d '1 second ago' src/link_app.S" "$root/kernel/Makefile"; then
  echo "FAIL: generated link_app.S needs a stable WSL timestamp" >&2
  status=1
fi
if ! grep -Eq '^link_app\.o: build_app$' "$root/kernel/Makefile"; then
  echo "FAIL: link_app.o must rebuild after generated app metadata" >&2
  status=1
fi

cat > "$tmp/satp.c" <<'EOF'
#include <timeros/os.h>

_Static_assert(SATP_SV39 == 0x8000000000000000ULL,
               "Sv39 mode must set SATP bit 63");
_Static_assert(MAKE_PAGETABLE(MAKE_SATP(0x12345ULL)) == 0x12345ULL,
               "SATP page-table extraction must preserve the PPN");
EOF

if ! riscv64-unknown-elf-gcc -Werror=overflow -nostdlib -fno-builtin \
  -march=rv64imafd_zicsr_zifencei -mabi=lp64d -mcmodel=medany \
  -I "$root/kernel/include" -c "$tmp/satp.c" -o "$tmp/satp.o" \
  >"$tmp/satp.out" 2>"$tmp/satp.err"; then
  cat "$tmp/satp.err" >&2
  echo "FAIL: SATP macros must compile without signed overflow" >&2
  status=1
fi

for elf in "$root/kernel/os.elf" "$root/trusted/build/trusted_fw.elf"
do
  if [ ! -f "$elf" ]; then
    echo "FAIL: missing $elf; run make m1-build first" >&2
    status=1
    continue
  fi
  if riscv64-unknown-elf-readelf -W -l "$elf" | \
    awk '$1 == "LOAD" && $7 ~ /W/ && $7 ~ /E/ { found = 1 } END { exit !found }'; then
    echo "FAIL: $elf contains a writable executable LOAD segment" >&2
    status=1
  fi
done

if [ "$status" -ne 0 ]; then
  exit "$status"
fi

echo "PASS: M1 build contracts"
