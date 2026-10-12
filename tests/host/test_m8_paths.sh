#!/usr/bin/env bash
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

expect_tier() {
  expected=$1
  shift
  actual=$(printf '%s\0' "$@" | "$root/scripts/m8-paths.sh")
  [ "$actual" = "$expected" ] || \
    fail "[$*] should select tier $expected, got $actual"
}

expect_tier none
expect_tier none README.md
expect_tier none README.zh-CN.md README.md docs/build-debug.md
expect_tier none README.md docs/build-debug.zh-CN.md THIRD_PARTY.md
expect_tier none LICENSE
expect_tier none docs/
expect_tier full kernel/src/main.c
expect_tier full trusted/main.c
expect_tier full trusted/port/portASM.S
expect_tier full platform/quard-star/boot/start.s
expect_tier full platform/quard-star/dts/quard_star_kernel_m8.dts
expect_tier full patches/qemu/0001-add-quard-star-machine.patch
expect_tier full patches/opensbi/series
expect_tier full third_party/fatfs.lock
expect_tier full user/initproc.c
expect_tier full scripts/m5-smoke.sh
expect_tier full scripts/m8-build.sh
expect_tier full tests/host/test_m8_contracts.sh
expect_tier full Makefile
expect_tier full .gitmodules
expect_tier full .gitattributes
expect_tier full .github/workflows/m8-smoke.yml
expect_tier full kernel/src/main.c README.md
expect_tier full some/unclassified/path.txt
expect_tier full kernel/src/new-feature.c

echo 'PASS: M8 changed-path tier selection'
