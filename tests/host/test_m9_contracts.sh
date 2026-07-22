#!/usr/bin/env bash
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
workflow=$root/.github/workflows/host-tests.yml
smoke_workflow=$root/.github/workflows/m8-smoke.yml
grep -Fq 'runs-on: ubuntu-24.04' "$workflow"
grep -Fq 'submodules: true' "$workflow"
grep -Fq 'submodules: true' "$smoke_workflow"
if grep -Fq 'submodules: recursive' "$workflow" "$smoke_workflow"; then
  echo 'FAIL: CI must initialize only direct project submodules' >&2
  exit 1
fi
grep -Fq 'run: make test-host' "$workflow"
grep -Fq 'uses: actions/checkout@v5' "$workflow"
grep -Fq 'device-tree-compiler' "$workflow"
grep -Fq 'gcc-riscv64-unknown-elf' "$workflow"
grep -Fq 'workflow_dispatch:' "$smoke_workflow"
grep -Fq 'run: make m8-build' "$smoke_workflow"
grep -Fq 'run: sudo -E make m8-smoke' "$smoke_workflow"
grep -Fq 'uses: actions/cache@v4' "$smoke_workflow"
grep -Fq 'uses: actions/upload-artifact@v4' "$smoke_workflow"
grep -Fq 'make m8-build' "$root/README.md"
grep -Fq 'make m8-smoke' "$root/README.md"
grep -Fq 'docs/build-debug.md' "$root/README.md"
grep -Fq 'docs/limitations.md' "$root/README.md"
grep -Fq 'not PMP-enforced memory isolation' "$root/docs/limitations.md"
echo 'PASS: M9 host CI and README contracts'
