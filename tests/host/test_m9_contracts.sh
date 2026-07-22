#!/usr/bin/env bash
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
workflow=$root/.github/workflows/host-tests.yml
grep -Fq 'runs-on: ubuntu-24.04' "$workflow"
grep -Fq 'submodules: recursive' "$workflow"
grep -Fq 'run: make test-host' "$workflow"
grep -Fq 'make m8-build' "$root/README.md"
grep -Fq 'make m8-smoke' "$root/README.md"
echo 'PASS: M9 host CI and README contracts'
