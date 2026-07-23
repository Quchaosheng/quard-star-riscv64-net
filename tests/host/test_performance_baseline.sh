#!/usr/bin/env bash
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

fail()
{
    echo "FAIL: $*" >&2
    exit 1
}

cat >"$tmp/m8.log" <<'EOF'
QS:STRESS_ALLOC_OPS:14000
QS:STRESS_MIGRATIONS:100
QS:STRESS_ELAPSED_TICKS:277882554
QS:TEST_PASS:m8-smoke
EOF
cat >"$tmp/m8.stats" <<'EOF'
{"elapsed_seconds":30.5,"tftp_bytes":1048576,"tftp_outstanding":0,"future_counter":3}
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

python3 "$root/scripts/perf-baseline.py" \
    --stage m8 \
    --commit 0123456789abcdef0123456789abcdef01234567 \
    --qemu-log "$tmp/m8.log" \
    --peer-stats "$tmp/m8.stats" \
    --json-out "$tmp/m8.json" \
    --markdown-out "$tmp/m8.md"

python3 - "$tmp/m8.json" "$tmp/m8.md" <<'PY'
import json
import pathlib
import sys

report = json.loads(pathlib.Path(sys.argv[1]).read_text(encoding="utf-8"))
markdown = pathlib.Path(sys.argv[2]).read_text(encoding="utf-8")
assert report["schema_version"] == 1
assert report["stage"] == "m8"
assert report["commit"] == "0123456789abcdef0123456789abcdef01234567"
assert report["guest"] == {
    "allocation_operations": 14000,
    "migrations": 100,
    "elapsed_ticks": 277882554,
    "pass_marker": "QS:TEST_PASS:m8-smoke",
}
assert report["peer"]["tftp_bytes"] == 1048576
assert report["peer"]["future_counter"] == 3
assert report["derived"]["tftp_bytes_per_second"] == 1048576 / 30.5
assert "must not be compared directly" in report["interpretation"]
assert markdown.startswith("# Performance Baseline: m8\n")
assert "| TFTP bytes | 1048576 |" in markdown
PY

python3 "$root/scripts/perf-baseline.py" \
    --stage m6c2-stress \
    --commit ABCDEF0123456789ABCDEF0123456789ABCDEF01 \
    --qemu-log "$tmp/stress.log" \
    --peer-stats "$tmp/stress.stats" \
    --json-out "$tmp/stress.json" \
    --markdown-out "$tmp/stress.md"

python3 - "$tmp/stress.json" "$tmp/stress.md" <<'PY'
import json
import pathlib
import sys

report = json.loads(pathlib.Path(sys.argv[1]).read_text(encoding="utf-8"))
markdown = pathlib.Path(sys.argv[2]).read_text(encoding="utf-8")
assert report["commit"] == "abcdef0123456789abcdef0123456789abcdef01"
assert report["guest"]["elapsed_ticks"] == 3177262479
assert report["peer"]["tcp_server_stress_handshakes"] == 108
assert report["peer"]["tcp_server_stress_parallel_peak"] == 8
assert report["peer"]["tcp_server_stress_reconnects"] == 100
assert report["derived"] == {}
assert "| Reconnects | 100 |" in markdown
PY

expect_fail()
{
    stage=$1
    log=$2
    stats=$3
    phrase=$4
    printf 'old-json\n' >"$tmp/keep.json"
    printf 'old-markdown\n' >"$tmp/keep.md"
    if python3 "$root/scripts/perf-baseline.py" \
        --stage "$stage" \
        --qemu-log "$log" \
        --peer-stats "$stats" \
        --json-out "$tmp/keep.json" \
        --markdown-out "$tmp/keep.md" 2>"$tmp/error"; then
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

expect_fail m8 "$tmp/m8.log" "$tmp/bad.stats" \
    'invalid peer stats JSON'
expect_fail m8 "$tmp/no-pass.log" "$tmp/m8.stats" \
    'missing pass marker'
expect_fail m8 "$tmp/m8.log" "$tmp/wrong-m8.stats" \
    'tftp_bytes must be 1048576'
expect_fail m6c2-stress "$tmp/stress.log" "$tmp/wrong-stress.stats" \
    'tcp_server_stress_reconnects must be 100'

mkdir "$tmp/bin"
cat >"$tmp/bin/python3" <<'EOF'
#!/bin/sh
exec /usr/bin/python3 "$@"
EOF
chmod +x "$tmp/bin/python3"
(
    cd "$tmp"
    PATH="$tmp/bin" python3 "$root/scripts/perf-baseline.py" \
        --stage m8 \
        --qemu-log "$tmp/m8.log" \
        --peer-stats "$tmp/m8.stats" \
        --json-out "$tmp/no-git.json" \
        --markdown-out "$tmp/no-git.md"
)
python3 - "$tmp/no-git.json" <<'PY'
import json
import pathlib
import sys

report = json.loads(pathlib.Path(sys.argv[1]).read_text(encoding="utf-8"))
assert report["commit"] is None
PY

echo 'PASS: performance baseline reporting'
