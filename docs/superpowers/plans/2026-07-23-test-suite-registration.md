# Test Suite Registration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Register every host test script in Make and run artifact-dependent build contracts only after complete M8 acceptance.

**Architecture:** A Makefile registration contract prevents tracked host tests from silently falling out of all test targets. Source-only M3 contracts join `test-host`; the real-build M1 contract gets a separate `test-build` target that the M8 workflow invokes after smoke acceptance.

**Tech Stack:** GNU Make, POSIX shell, GitHub Actions YAML, RISC-V GNU toolchain

---

### Task 1: Register every host test script

**Files:**
- Modify: `tests/host/test_m0_scripts.sh`
- Modify: `Makefile`

- [ ] **Step 1: Add the failing Makefile registration contract**

Add after the executable-mode loop in `test_m0_scripts.sh`:

```sh
make -s -n -C "$root" test-host test-build > "$tmp/test-targets"
git -C "$root" ls-files 'tests/host/test_*.sh' | while IFS= read -r test; do
  awk -v command="./$test" \
    '$1 == command { found = 1 } END { exit !found }' "$tmp/test-targets" || \
    fail "$test is not reachable from test-host or test-build"
done
```

- [ ] **Step 2: Run M0 and verify RED**

Run:

```sh
./tests/host/test_m0_scripts.sh
```

Expected: exit 1 naming `tests/host/test_m1_build_contracts.sh` or
`tests/host/test_m3_contracts.sh` as unreachable.

- [ ] **Step 3: Register the source-only and build-only tests**

Add `test-build` to the Makefile `.PHONY` declaration. Add the M3 source
contract beside the existing M3 host checks:

```make
	./tests/host/test_m3_contracts.sh
```

Add a separate target after `test-host`:

```make
test-build:
	./tests/host/test_m1_build_contracts.sh
```

- [ ] **Step 4: Run the focused contracts and verify GREEN**

Run:

```sh
./tests/host/test_m0_scripts.sh
./tests/host/test_m3_contracts.sh
make -n test-build
```

Expected: M0 and M3 print `PASS`; the dry run prints exactly
`./tests/host/test_m1_build_contracts.sh`.

- [ ] **Step 5: Commit the registration boundary**

Run:

```sh
git diff --check
git add Makefile tests/host/test_m0_scripts.sh
git commit -m "test: register complete host suite"
```

Expected: the commit contains only the Makefile and M0 contract changes.

### Task 2: Run build contracts after M8 acceptance

**Files:**
- Modify: `tests/host/test_m9_contracts.sh`
- Modify: `.github/workflows/m8-smoke.yml`

- [ ] **Step 1: Add the failing M8 workflow contract**

Add after the existing M8 smoke command assertion:

```sh
job_count=$(grep -Fc '  qemu-smoke:' "$smoke_workflow" || true)
if [ "$job_count" -ne 1 ]; then
  echo 'FAIL: M8 CI must define exactly one qemu-smoke job' >&2
  exit 1
fi
smoke_job=$tmp/qemu-smoke.yml
awk '
  $0 == "  qemu-smoke:" { found = 1 }
  found && /^  [[:alnum:]_-]+:$/ && $0 != "  qemu-smoke:" { exit }
  found { print }
' "$smoke_workflow" > "$smoke_job"
smoke_count=$(grep -Ec '^[[:space:]]+run:[[:space:]]+sudo -E make m8-smoke[[:space:]]*$' \
  "$smoke_job" || true)
build_test_count=$(grep -Ec '^[[:space:]]+run:[[:space:]]+make test-build[[:space:]]*$' \
  "$smoke_job" || true)
if [ "$smoke_count" -ne 1 ] || [ "$build_test_count" -ne 1 ]; then
  echo 'FAIL: qemu-smoke must run smoke and build contracts exactly once' >&2
  exit 1
fi
smoke_line=$(grep -En '^[[:space:]]+run:[[:space:]]+sudo -E make m8-smoke[[:space:]]*$' \
  "$smoke_job" | cut -d: -f1)
build_test_line=$(grep -En '^[[:space:]]+run:[[:space:]]+make test-build[[:space:]]*$' \
  "$smoke_job" | cut -d: -f1)
if [ -z "$smoke_line" ] || [ -z "$build_test_line" ] ||
   [ "$smoke_line" -ge "$build_test_line" ]; then
  echo 'FAIL: build contracts must run after M8 smoke acceptance' >&2
  exit 1
fi
```

- [ ] **Step 2: Run M9 and verify RED**

Run:

```sh
./tests/host/test_m9_contracts.sh
```

Expected: exit 1 with
`FAIL: qemu-smoke must run smoke and build contracts exactly once`.

- [ ] **Step 3: Add the post-smoke build-contract step**

Add immediately after `Run M8 QEMU/TAP smoke` in `m8-smoke.yml`:

```yaml
      - name: Run build contracts
        run: make test-build
```

- [ ] **Step 4: Run M9 and verify GREEN**

Run:

```sh
./tests/host/test_m9_contracts.sh
```

Expected: `PASS: M9 host CI and README contracts`.

- [ ] **Step 5: Commit the M8 workflow boundary**

Run:

```sh
git diff --check
git add .github/workflows/m8-smoke.yml tests/host/test_m9_contracts.sh
git commit -m "ci: run build contracts after m8 smoke"
```

Expected: the commit contains only the M8 workflow and M9 contract changes.

### Task 3: Restore the default kernel build boundary

**Files:**
- Modify: `kernel/src/task.c`
- Modify: `docs/superpowers/specs/2026-07-23-test-suite-registration-design.md`
- Modify: `docs/superpowers/plans/2026-07-23-test-suite-registration.md`

- [ ] **Step 1: Reproduce the default-build failure**

Run `make test-build` with complete submodules and the existing trusted ELF.

Expected: the default `FATFS=0` link fails with `undefined reference to
file_close_owner`.

- [ ] **Step 2: Restore the existing M7E feature boundary**

Change the exit cleanup in `kernel/src/task.c` to:

```c
#ifdef QS_M7E_TEST
    file_close_owner(p->pid);
#endif
```

- [ ] **Step 3: Verify the default build and M7E contracts**

Run:

```sh
make test-build
./tests/host/test_m7e_file.sh
./tests/host/test_m7e_contracts.sh
./tests/host/test_m8_contracts.sh
```

Expected: the build prints `PASS: M1 build contracts`; all three focused host
tests pass.

- [ ] **Step 4: Commit the feature-gate repair**

Run:

```sh
git diff --check
git add kernel/src/task.c \
  docs/superpowers/specs/2026-07-23-test-suite-registration-design.md \
  docs/superpowers/plans/2026-07-23-test-suite-registration.md
git commit -m "fix: guard m7e file cleanup"
```

Expected: the commit contains the one kernel feature gate and the matching
design and plan corrections.

### Task 4: Verify both dependency environments

**Files:**
- Verify only; no tracked file changes

- [ ] **Step 1: Run the complete no-submodule host suite**

Create a fresh clone with `git clone --no-recurse-submodules`, assert all five
entries from `git submodule status` begin with `-`, and run:

```sh
make test-host
```

Expected: M0-M9, including `PASS: M3 source contracts`, and the performance
reporter all pass without fetching submodules.

- [ ] **Step 2: Verify the prepared build environment**

In the feature worktree, verify that all five submodules are initialized and
that `trusted/build/trusted_fw.elf` exists, then run:

```sh
make test-build
```

Expected: user and kernel builds succeed, both ELF files contain no writable
executable LOAD segment, and the script prints `PASS: M1 build contracts`.

- [ ] **Step 3: Run final repository checks**

Run:

```sh
git diff --check origin/main...HEAD
git status --short
```

Expected: diff check exits 0 and the feature worktree has no uncommitted
tracked or untracked files.
