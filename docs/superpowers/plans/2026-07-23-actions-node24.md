# GitHub Actions Node.js 24 Upgrade Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Remove the M8 workflow's Node.js 20 deprecation warning by moving its cache and artifact actions to supported Node.js 24 majors.

**Architecture:** Keep the workflow structure and all existing inputs unchanged. Strengthen the existing M9 source contract first, then make the two minimal action-reference changes needed to satisfy it.

**Tech Stack:** GitHub Actions YAML, POSIX shell, GNU Make

---

### Task 1: Require Node.js 24 action majors

**Files:**
- Modify: `tests/host/test_m9_contracts.sh`
- Modify: `.github/workflows/m8-smoke.yml`

- [ ] **Step 1: Write the failing workflow contract**

Change the two existing action assertions in `test_m9_contracts.sh` to:

```sh
grep -Fq 'uses: actions/cache@v6' "$smoke_workflow"
grep -Fq 'uses: actions/upload-artifact@v7' "$smoke_workflow"
```

- [ ] **Step 2: Run M9 and verify RED**

Run:

```sh
./tests/host/test_m9_contracts.sh
```

Expected: exit 1 because `m8-smoke.yml` still references `actions/cache@v4`
and `actions/upload-artifact@v4`.

- [ ] **Step 3: Make the minimal workflow upgrade**

Change only these action references in `m8-smoke.yml`:

```yaml
      - uses: actions/cache@v6
```

```yaml
      - name: Upload serial logs
        if: always()
        uses: actions/upload-artifact@v7
```

Keep every `with:` input unchanged.

- [ ] **Step 4: Run M9 and verify GREEN**

Run:

```sh
./tests/host/test_m9_contracts.sh
git diff --check
```

Expected: `PASS: M9 host CI and README contracts`; diff check exits 0.

- [ ] **Step 5: Commit the upgrade**

Run:

```sh
git add .github/workflows/m8-smoke.yml tests/host/test_m9_contracts.sh
git commit -m "ci: upgrade actions to node24"
```

Expected: the commit contains two action-reference changes and two matching
contract-reference changes.

### Task 2: Verify repository and hosted-runner behavior

**Files:**
- Verify only; no tracked file changes

- [ ] **Step 1: Run the complete host suite without initialized submodules**

Create a fresh no-submodule clone at the feature commit, confirm all five
`git submodule status` lines begin with `-`, and run:

```sh
make test-host
```

Expected: every registered host test passes, including
`PASS: M9 host CI and README contracts`.

- [ ] **Step 2: Run build contracts with complete dependencies**

In the prepared feature worktree, run:

```sh
make test-build
```

Expected: `PASS: M1 build contracts`.

- [ ] **Step 3: Run final repository checks**

Run:

```sh
git diff --check origin/main...HEAD
git status --short
```

Expected: diff check exits 0 and the worktree is clean.

- [ ] **Step 4: Verify the hosted M8 workflow**

Push `fix/actions-node24`, open a pull request, and manually dispatch
`m8-smoke` on the feature branch. Verify:

- `Build M8 firmware` succeeds.
- `Run M8 QEMU/TAP smoke` succeeds.
- `Run build contracts` succeeds.
- `Upload serial logs` succeeds.
- The run has no Node.js 20 deprecation annotation.
- The artifact contains `QS:TEST_PASS:m8-smoke`,
  `QS:TRUSTED_SCHED_OK`, `QS:PMP_UNTRUSTED_DENY_OK`, and
  `QS:PMP_TRUSTED_DENY_OK`.

- [ ] **Step 5: Merge only after all checks pass**

Merge with a merge commit, wait for the resulting `main` host workflow to
pass, then delete the remote feature branch.
