# Performance Baseline Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Convert existing QEMU and TAP-peer acceptance artifacts into validated JSON and Markdown performance baseline reports.

**Architecture:** A dependency-free Python CLI parses stable `QS:` counters and the existing peer JSON, validates generic and stage-specific invariants, renders both outputs in memory, and atomically replaces each destination. A shell host test supplies synthetic artifacts and checks success, rejection, Git fallback, and output preservation.

**Tech Stack:** Python 3 standard library, POSIX shell, GNU Make, existing QEMU/TAP artifacts.

---

### Task 1: Add The Reporter Contract Test

**Files:**
- Create: `tests/host/test_performance_baseline.sh`

- [ ] **Step 1: Create synthetic M8 and stress artifacts**

Create a temporary directory with these exact inputs:

```sh
cat >"$tmp/m8.log" <<'EOF'
QS:STRESS_ALLOC_OPS:14000
QS:STRESS_MIGRATIONS:100
QS:STRESS_ELAPSED_TICKS:277882554
QS:TEST_PASS:m8-smoke
EOF
cat >"$tmp/m8.stats" <<'EOF'
{"elapsed_seconds":30.5,"tftp_bytes":1048576,"tftp_outstanding":0}
EOF

cat >"$tmp/stress.log" <<'EOF'
QS:STRESS_ALLOC_OPS:100000
QS:STRESS_MIGRATIONS:10000
QS:STRESS_ELAPSED_TICKS:3177262479
QS:TEST_PASS:m6c2-stress
EOF
cat >"$tmp/stress.stats" <<'EOF'
{"elapsed_seconds":239.0,"tcp_server_stress_handshakes":108,"tcp_server_stress_echo":108,"tcp_server_stress_parallel_peak":8,"tcp_server_stress_reconnects":100,"tcp_server_stress_fin":108,"tcp_server_stress_outstanding":0,"tcp_server_stress_live":0}
EOF
```

- [ ] **Step 2: Assert deterministic successful output**

Invoke `scripts/perf-baseline.py` for both stages with a fixed 40-character
commit. Use Python assertions to check schema version, parsed counters, commit,
the M8 derived rate, the fixed interpretation warning, and stable Markdown
headings:

```sh
python3 "$root/scripts/perf-baseline.py" \
  --stage m8 --commit 0123456789abcdef0123456789abcdef01234567 \
  --qemu-log "$tmp/m8.log" --peer-stats "$tmp/m8.stats" \
  --json-out "$tmp/m8.json" --markdown-out "$tmp/m8.md"

python3 - "$tmp/m8.json" "$tmp/m8.md" <<'PY'
import json, pathlib, sys
report = json.loads(pathlib.Path(sys.argv[1]).read_text(encoding="utf-8"))
markdown = pathlib.Path(sys.argv[2]).read_text(encoding="utf-8")
assert report["schema_version"] == 1
assert report["stage"] == "m8"
assert report["commit"] == "0123456789abcdef0123456789abcdef01234567"
assert report["guest"]["elapsed_ticks"] == 277882554
assert report["peer"]["tftp_bytes"] == 1048576
assert report["derived"]["tftp_bytes_per_second"] == 1048576 / 30.5
assert "must not be compared directly" in report["interpretation"]
assert markdown.startswith("# Performance Baseline: m8\n")
assert "| TFTP bytes | 1048576 |" in markdown
PY
```

- [ ] **Step 3: Assert rejection and output preservation**

Copy known sentinel contents to both destinations, then run malformed JSON,
missing pass marker, wrong M8 byte count, and wrong stress reconnect count.
Use this helper so every invocation must fail, include one expected phrase on
stderr, and leave both sentinels unchanged:

```sh
expect_fail() {
  stage=$1
  log=$2
  stats=$3
  phrase=$4
  printf 'old-json\n' >"$tmp/keep.json"
  printf 'old-markdown\n' >"$tmp/keep.md"
  if python3 "$root/scripts/perf-baseline.py" --stage "$stage" \
    --qemu-log "$log" --peer-stats "$stats" \
    --json-out "$tmp/keep.json" --markdown-out "$tmp/keep.md" \
    2>"$tmp/error"; then
    fail "$stage invalid fixture passed"
  fi
  grep -Fq "$phrase" "$tmp/error" || fail "missing error: $phrase"
  [ "$(cat "$tmp/keep.json")" = old-json ] || fail "JSON was replaced"
  [ "$(cat "$tmp/keep.md")" = old-markdown ] || fail "Markdown was replaced"
}

printf '{\n' >"$tmp/bad.stats"
sed '/QS:TEST_PASS:/d' "$tmp/m8.log" >"$tmp/no-pass.log"
printf '%s\n' \
  '{"elapsed_seconds":30.5,"tftp_bytes":1,"tftp_outstanding":0}' \
  >"$tmp/wrong-m8.stats"
sed 's/"tcp_server_stress_reconnects":100/"tcp_server_stress_reconnects":99/' \
  "$tmp/stress.stats" >"$tmp/wrong-stress.stats"

expect_fail m8 "$tmp/m8.log" "$tmp/bad.stats" 'invalid peer stats JSON'
expect_fail m8 "$tmp/no-pass.log" "$tmp/m8.stats" 'missing pass marker'
expect_fail m8 "$tmp/m8.log" "$tmp/wrong-m8.stats" 'tftp_bytes must be 1048576'
expect_fail m6c2-stress "$tmp/stress.log" "$tmp/wrong-stress.stats" \
  'tcp_server_stress_reconnects must be 100'
```

Run the valid command from `$tmp`, after setting `PATH` to a temporary bin
directory containing `python3` but no `git`, without `--commit`; assert that
JSON contains `"commit": null`.

- [ ] **Step 4: Run the test and verify RED**

Run:

```sh
./tests/host/test_performance_baseline.sh
```

Expected: FAIL because `scripts/perf-baseline.py` does not exist.

- [ ] **Step 5: Commit the failing contract**

```sh
git add tests/host/test_performance_baseline.sh
git commit -m "test: define performance baseline reports"
```

---

### Task 2: Implement The Baseline Reporter

**Files:**
- Create: `scripts/perf-baseline.py`

- [ ] **Step 1: Add argument parsing and typed input loading**

Implement these entry points and constants:

```python
#!/usr/bin/env python3
import argparse
import json
import math
import os
import pathlib
import re
import subprocess
import sys
import tempfile

SCHEMA_VERSION = 1
INTERPRETATION = (
    "Results from different hosts or QEMU configurations must not be "
    "compared directly."
)
COUNTERS = {
    "QS:STRESS_ALLOC_OPS:": "allocation_operations",
    "QS:STRESS_MIGRATIONS:": "migrations",
    "QS:STRESS_ELAPSED_TICKS:": "elapsed_ticks",
}

def parse_args():
    parser = argparse.ArgumentParser()
    parser.add_argument("--stage", required=True)
    parser.add_argument("--commit")
    parser.add_argument("--qemu-log", required=True, type=pathlib.Path)
    parser.add_argument("--peer-stats", required=True, type=pathlib.Path)
    parser.add_argument("--json-out", required=True, type=pathlib.Path)
    parser.add_argument("--markdown-out", required=True, type=pathlib.Path)
    return parser.parse_args()
```

Read both files as strict UTF-8. Decode peer stats with `json.loads`, require a
JSON object, reject booleans, strings, non-finite floats, and negative numeric
counters, and preserve all valid numeric keys.

- [ ] **Step 2: Parse and validate guest evidence**

Scan complete log lines. Require each counter exactly once, parse unsigned base
10 integers, and require exactly one `QS:TEST_PASS:<name>` marker. Reject zero
elapsed ticks. Build:

```python
guest = {
    "allocation_operations": value,
    "migrations": value,
    "elapsed_ticks": value,
    "pass_marker": marker,
}
```

Require positive `elapsed_seconds` in peer stats. For `m8`, require pass marker
`QS:TEST_PASS:m8-smoke`, `tftp_bytes == 1048576`, and
`tftp_outstanding == 0`. For `m6c2-stress`, require its pass marker and the
exact 108/108/8/100/108/0/0 stress values from the design.

- [ ] **Step 3: Resolve commit provenance safely**

Validate explicit commits with `re.fullmatch(r"[0-9a-fA-F]{7,40}", value)` and
normalize to lowercase. Without `--commit`, run:

```python
subprocess.run(
    ["git", "rev-parse", "HEAD"],
    text=True, capture_output=True, check=False,
)
```

Use the result only when the command succeeds and returns 40 hexadecimal
characters; otherwise store `None` without printing Git diagnostics.

- [ ] **Step 4: Build and render one validated model**

Build the report in this insertion order so JSON output is deterministic:

```python
report = {
    "schema_version": SCHEMA_VERSION,
    "stage": args.stage,
    "commit": commit,
    "guest": guest,
    "peer": peer,
    "derived": derived,
    "interpretation": INTERPRETATION,
}
```

Only calculate `tftp_bytes_per_second` when `tftp_bytes` exists, using the
validated positive `elapsed_seconds`. Render JSON with `indent=2` and a final
newline. Render Markdown with stage, commit or `unknown`, guest counters, a
stage-relevant peer table, derived values, and the interpretation warning.

- [ ] **Step 5: Replace output files only after complete rendering**

Create missing parent directories. For each destination, write to a named
temporary sibling with strict UTF-8, flush and `os.fsync`, then call
`os.replace`. Do not open either destination until both complete strings have
been rendered.

Wrap `main()` so all expected validation and I/O failures print exactly
`error: <message>` to stderr and exit 1 without a traceback.

- [ ] **Step 6: Run the focused test and verify GREEN**

Run:

```sh
./tests/host/test_performance_baseline.sh
```

Expected: `PASS: performance baseline reporting`.

- [ ] **Step 7: Commit the implementation**

```sh
git add scripts/perf-baseline.py
git commit -m "feat: report acceptance performance baselines"
```

---

### Task 3: Integrate Host Verification And Usage Documentation

**Files:**
- Modify: `Makefile`
- Create: `docs/performance-baseline.md`
- Modify: `README.md`

- [ ] **Step 1: Prove the host suite does not run the new test**

Run:

```sh
make -n test-host | grep -F './tests/host/test_performance_baseline.sh'
```

Expected: FAIL with no match.

- [ ] **Step 2: Add the host test to `Makefile`**

Append this command after the M9 contract test in `test-host`:

```make
	./tests/host/test_performance_baseline.sh
```

Run the command from Step 1 again. Expected: one matching line.

- [ ] **Step 3: Document reproducible report generation**

Create `docs/performance-baseline.md` with:

- the M8 and `m6c2-stress` workload definitions;
- exact build, smoke, and reporter commands;
- metric definitions and units;
- the same-host/same-QEMU comparison rule;
- an explicit statement that hosted-runner values are observations, not gates;
- a baseline table populated only after Task 4 produces fresh artifacts.

Add one README sentence linking to the document after the verification section.

- [ ] **Step 4: Run focused and full host verification**

Run in WSL2 Ubuntu 24.04:

```sh
./tests/host/test_performance_baseline.sh
make test-host
```

Expected: the focused test and every M0-M9 test print `PASS` and Make exits 0.

- [ ] **Step 5: Commit integration and documentation**

```sh
git add Makefile README.md docs/performance-baseline.md
git commit -m "docs: explain reproducible performance baselines"
```

---

### Task 4: Capture The First Traceable Baseline

**Files:**
- Modify: `docs/performance-baseline.md`
- Generated, not committed: `out/performance/*.json`, `out/performance/*.md`

- [ ] **Step 1: Build and run M8 on the implementation commit**

In the Windows-hosted worktree under WSL2 Ubuntu 24.04, record the commit with
Windows Git because the worktree's `.git` file contains a Windows path, then
run:

```sh
make m8-build
sudo -v
sudo -E make m8-smoke
commit=$(git.exe rev-parse HEAD | tr -d '\r')
python3 scripts/perf-baseline.py \
  --stage m8 --commit "$commit" \
  --qemu-log out/m8/qemu.log \
  --peer-stats out/m8/m5-peer.stats \
  --json-out out/performance/m8.json \
  --markdown-out out/performance/m8.md
```

Expected: M8 smoke exits 0 and both report files exist.

- [ ] **Step 2: Build and run the cumulative TCP stress workload**

```sh
sudo -E make m6c2-stress
commit=$(git.exe rev-parse HEAD | tr -d '\r')
python3 scripts/perf-baseline.py \
  --stage m6c2-stress --commit "$commit" \
  --qemu-log out/m6c2-stress/qemu.log \
  --peer-stats out/m6c2-stress/m5-peer.stats \
  --json-out out/performance/m6c2-stress.json \
  --markdown-out out/performance/m6c2-stress.md
```

Expected: stress smoke exits 0, reports show 108 completed connections, peak 8,
100 reconnects, and no live/outstanding connections.

- [ ] **Step 3: Record environment and observed values**

Add one table row per run to `docs/performance-baseline.md` containing date,
full commit, WSL2 Ubuntu 24.04, stage, guest elapsed ticks, host elapsed seconds,
and stage evidence. Do not claim an improvement because this is the first
sample.

- [ ] **Step 4: Verify generated reports and repository cleanliness**

Run:

```sh
python3 -m json.tool out/performance/m8.json >/dev/null
python3 -m json.tool out/performance/m6c2-stress.json >/dev/null
make test-host
git diff --check
git status --short
```

Expected: JSON validation and host suite pass; only the intended documentation
change is tracked, while `out/` remains ignored.

- [ ] **Step 5: Commit the observed baseline**

```sh
git add docs/performance-baseline.md
git commit -m "docs: record initial performance baseline"
```

---

### Task 5: Final Review And Publication

**Files:**
- Review all files changed since `origin/main`.

- [ ] **Step 1: Review the branch diff**

Run:

```sh
git diff --check origin/main...HEAD
git diff --stat origin/main...HEAD
git log --oneline origin/main..HEAD
```

Confirm there are no kernel, protocol, third-party, or generated artifact
changes.

- [ ] **Step 2: Push the reviewed branch**

```sh
git push -u origin feat/performance-baseline
```

Expected: the remote branch points to the reviewed local HEAD.

- [ ] **Step 3: Run final verification from a WSL-native clone**

Clone `feat/performance-baseline` from GitHub into a new WSL `/tmp` directory,
run `make test-host`, and invoke the reporter once with the synthetic M8
fixture. Expected: all commands exit 0 without Git worktree path warnings.

- [ ] **Step 4: Open a pull request**

Create a ready PR describing the reporter, validation boundaries, fresh M8 and
stress evidence, and the absence of performance gates or kernel changes.
