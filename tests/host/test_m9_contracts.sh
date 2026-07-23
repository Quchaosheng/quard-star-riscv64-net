#!/usr/bin/env bash
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
workflow=$root/.github/workflows/host-tests.yml
smoke_workflow=$root/.github/workflows/m8-smoke.yml
grep -Fq 'runs-on: ubuntu-24.04' "$workflow"
if grep -Eq '^[[:space:]]+submodules:' "$workflow" ||
   grep -Eq '^[[:space:]]*(run:[[:space:]]*)?git[[:space:]]+submodule([[:space:]]|$)' \
     "$workflow"; then
  echo 'FAIL: host CI must not fetch build-only submodules' >&2
  exit 1
fi
grep -Eq '^[[:space:]]+submodules:[[:space:]]+true[[:space:]]*$' \
  "$smoke_workflow"
if grep -Fq 'submodules: recursive' "$workflow" "$smoke_workflow"; then
  echo 'FAIL: CI must initialize only direct project submodules' >&2
  exit 1
fi
grep -Fq 'run: make test-host' "$workflow"
grep -Fq 'uses: actions/checkout@v5' "$workflow"
grep -Fq 'device-tree-compiler' "$workflow"
grep -Fq 'gcc-riscv64-unknown-elf' "$workflow"
grep -Fq 'workflow_dispatch:' "$smoke_workflow"
grep -Fq 'run: ./scripts/prepare-fatfs.sh' "$smoke_workflow"
grep -Fq 'run: make m8-build' "$smoke_workflow"
grep -Fq 'run: sudo -E make m8-smoke' "$smoke_workflow"
if ! grep -Eq '^[[:space:]]+run:[[:space:]]+make test-build[[:space:]]*$' \
  "$smoke_workflow"; then
  echo 'FAIL: M8 CI must run build contracts' >&2
  exit 1
fi
smoke_line=$(grep -En '^[[:space:]]+run:[[:space:]]+sudo -E make m8-smoke[[:space:]]*$' \
  "$smoke_workflow" | cut -d: -f1)
build_test_line=$(grep -En '^[[:space:]]+run:[[:space:]]+make test-build[[:space:]]*$' \
  "$smoke_workflow" | cut -d: -f1)
if [ -z "$smoke_line" ] || [ -z "$build_test_line" ] ||
   [ "$smoke_line" -ge "$build_test_line" ]; then
  echo 'FAIL: build contracts must run after M8 smoke acceptance' >&2
  exit 1
fi
grep -Fq 'uses: actions/cache@v4' "$smoke_workflow"
grep -Fq 'uses: actions/upload-artifact@v4' "$smoke_workflow"
grep -Fq 'make m8-build' "$root/README.md"
grep -Fq 'make m8-smoke' "$root/README.md"
grep -Fq 'docs/build-debug.md' "$root/README.md"
grep -Fq 'docs/limitations.md' "$root/README.md"
grep -Fq 'The original source repositories are no longer publicly available' \
  "$root/docs/source-migration.md"
grep -Fq '641f42560999ab00ad7ba01169cb2b3d723d8c48' \
  "$root/docs/source-migration.md"
grep -Fq '32e4988e2d482ad3ee406e36b5adbd84a63c8e9e' \
  "$root/docs/source-migration.md"
if grep -Fq 'https://github.com/Quchaosheng/tiny-tcpip-stack' \
    "$root/docs/source-migration.md" ||
   grep -Fq 'https://github.com/Quchaosheng/quard-star-riscv64-kernel' \
    "$root/docs/source-migration.md"; then
  echo 'FAIL: source migration links to unavailable repositories' >&2
  exit 1
fi
grep -Fq 'current release line is `v1.0.0`' "$root/README.md"
grep -Fq 'PMP-enforced memory isolation' "$root/README.md"
grep -Fq 'QS:TRUSTED_SCHED_OK' "$root/README.md"
grep -Fq 'QS:PMP_UNTRUSTED_DENY_OK' "$root/README.md"
grep -Fq 'QS:PMP_TRUSTED_DENY_OK' "$root/README.md"
grep -Fq 'QEMU-only' "$root/docs/limitations.md"
grep -Fq 'load, store, and instruction access faults' \
  "$root/docs/limitations.md"
if grep -Fq 'not PMP-enforced memory isolation' "$root/docs/limitations.md"; then
  echo 'FAIL: limitations still describe the removed allmem boundary' >&2
  exit 1
fi
if grep -Fq '`v1.0.0` is not published' "$root/README.md"; then
  echo 'FAIL: README still describes v1.0.0 as unpublished' >&2
  exit 1
fi
if git -C "$root" ls-files '*.pcap' | grep -q .; then
  echo 'FAIL: generated packet captures must not be tracked' >&2
  exit 1
fi
echo 'PASS: M9 host CI and README contracts'
