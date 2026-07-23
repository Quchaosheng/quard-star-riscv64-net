# Host CI Submodule Checkout Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Run the host-only GitHub Actions suite without fetching third-party submodules while preserving complete submodule checkout for M8 builds.

**Architecture:** The host workflow relies only on the repository checkout and Ubuntu packages, so it will use the default checkout behavior. The existing M9 source contract will enforce the split: host CI must not request submodules, while M8 must continue to initialize direct submodules.

**Tech Stack:** GitHub Actions YAML, POSIX shell contract tests, Git

---

### Task 1: Make the checkout boundary executable

**Files:**
- Modify: `tests/host/test_m9_contracts.sh`
- Modify: `.github/workflows/host-tests.yml`

- [ ] **Step 1: Replace the host submodule assertion with a failing negative contract**

Replace:

```sh
grep -Fq 'submodules: true' "$workflow"
grep -Fq 'submodules: true' "$smoke_workflow"
```

with:

```sh
if grep -Eq '^[[:space:]]+submodules:' "$workflow" ||
   grep -Eq '^[[:space:]]*(run:[[:space:]]*)?git[[:space:]]+submodule([[:space:]]|$)' \
     "$workflow"; then
  echo 'FAIL: host CI must not fetch build-only submodules' >&2
  exit 1
fi
grep -Eq '^[[:space:]]+submodules:[[:space:]]+true[[:space:]]*$' \
  "$smoke_workflow"
```

- [ ] **Step 2: Run the focused contract and verify RED**

Run:

```sh
./tests/host/test_m9_contracts.sh
```

Expected: exit 1 with `FAIL: host CI must not fetch build-only submodules`
because `host-tests.yml` still contains `submodules: true`.

- [ ] **Step 3: Remove host-only submodule checkout**

Change the host workflow checkout step from:

```yaml
      - uses: actions/checkout@v5
        with:
          submodules: true
```

to:

```yaml
      - uses: actions/checkout@v5
```

Do not change `.github/workflows/m8-smoke.yml`.

- [ ] **Step 4: Run the focused contract and verify GREEN**

Run:

```sh
./tests/host/test_m9_contracts.sh
```

Expected: `PASS: M9 host CI and README contracts`

- [ ] **Step 5: Run the complete host suite**

Run:

```sh
make test-host
```

Expected: every M0-M9 and performance reporter test prints `PASS`; Make exits
0.

- [ ] **Step 6: Check and commit the implementation**

Run:

```sh
git diff --check
git add .github/workflows/host-tests.yml tests/host/test_m9_contracts.sh
git commit -m "fix: isolate host ci from build submodules"
```

Expected: diff check exits 0 and the commit contains exactly the workflow and
contract changes.

### Task 2: Prove a no-submodule checkout is sufficient

**Files:**
- Verify only; no tracked file changes

- [ ] **Step 1: Create a disposable clone without submodules**

Run from WSL2:

```sh
clone=$(mktemp -d)
git clone --no-recurse-submodules \
  --branch fix/host-ci-submodule-checkout \
  file:///mnt/c/Users/qucha/Documents/tcp/.git \
  "$clone/repo"
cd "$clone/repo"
```

Expected: the clone succeeds and each `third_party/*` gitlink remains
uninitialized.

- [ ] **Step 2: Assert the submodules are uninitialized**

Run:

```sh
test "$(git submodule status | sed -n 's/^-.*$/uninitialized/p' | wc -l)" -eq 5
```

Expected: exit 0.

- [ ] **Step 3: Run host tests in the disposable clone**

Run:

```sh
make test-host
```

Expected: every host test passes without fetching a submodule.

- [ ] **Step 4: Verify the feature worktree remains clean**

Run in the feature worktree:

```sh
git status --short
git show --stat --oneline HEAD
```

Expected: no status output and the implementation commit changes only
`.github/workflows/host-tests.yml` and `tests/host/test_m9_contracts.sh`.
