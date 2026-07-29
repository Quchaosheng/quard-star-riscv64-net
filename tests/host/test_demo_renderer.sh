#!/usr/bin/env bash
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
workflow=$root/.github/workflows/m8-smoke.yml
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

qemu=$tmp/qemu.log
trusted=$tmp/trusted.log
stats=$tmp/stats.json

cat >"$qemu" <<'EOF'
QS:BOOT_OK
QS:KERNEL_READY
QS:HART_ONLINE:0
QS:HART_ONLINE:1
QS:HART_ONLINE:2
QS:HART_ONLINE:3
QS:HART_ONLINE:4
QS:HART_ONLINE:5
QS:HART_ONLINE:6
QS:STRESS_ALLOC_OPS:14000
QS:STRESS_MIGRATIONS:100
QS:PMP_UNTRUSTED_DENY_OK
QS:M5_PING_OK
QS:M7A_DNS_RESOLVE_OK
QS:M7B_HTTP_RESPONSE_OK
QS:M7C_NTP_RESPONSE_OK
QS:M7E_TFTP_1M_OK
QS:M7E_TFTP_SHA256_OK
QS:TEST_PASS:m8-smoke
EOF

cat >"$trusted" <<'EOF'
QS:TRUSTED_READY
QS:TRUSTED_SCHED_OK
QS:PMP_TRUSTED_DENY_OK
EOF

cat >"$stats" <<'EOF'
{
  "raw_frames": 32,
  "dns_replies": 2,
  "http_responses": 1,
  "http_outstanding": 0,
  "ntp_replies": 1,
  "tftp_bytes": 1048576,
  "tftp_data": 2049,
  "tftp_acks": 2049,
  "tftp_outstanding": 0,
  "elapsed_seconds": 18.25
}
EOF

python3 "$root/scripts/render-demo.py" \
  --qemu-log "$qemu" --trusted-log "$trusted" --peer-stats "$stats" \
  --commit fixture --validate-only >"$tmp/summary.json"

python3 - "$tmp/summary.json" <<'PY'
import json
import pathlib
import sys

data = json.loads(pathlib.Path(sys.argv[1]).read_text(encoding="utf-8"))
assert data["source_commit"] == "fixture"
assert data["acceptance"]["ordinary_harts"] == 7
assert data["acceptance"]["trusted_harts"] == 1
assert data["acceptance"]["tftp_bytes"] == 1048576
assert data["acceptance"]["pass_marker"] == "QS:TEST_PASS:m8-smoke"
assert len(data["source_sha256"]["qemu_log"]) == 64
PY

sed '/QS:TRUSTED_SCHED_OK/d' "$trusted" >"$tmp/missing-trusted.log"
if python3 "$root/scripts/render-demo.py" \
  --qemu-log "$qemu" --trusted-log "$tmp/missing-trusted.log" \
  --peer-stats "$stats" --validate-only >/dev/null 2>&1; then
  echo 'FAIL: missing trusted scheduler evidence must reject demo rendering' >&2
  exit 1
fi

sed 's/"tftp_bytes": 1048576/"tftp_bytes": 512/' "$stats" >"$tmp/bad-stats.json"
if python3 "$root/scripts/render-demo.py" \
  --qemu-log "$qemu" --trusted-log "$trusted" \
  --peer-stats "$tmp/bad-stats.json" --validate-only >/dev/null 2>&1; then
  echo 'FAIL: incomplete TFTP evidence must reject demo rendering' >&2
  exit 1
fi

grep -Fq 'demo: m8-build' "$root/Makefile"
grep -Fq 'sudo -E $(MAKE) m8-smoke' "$root/Makefile"
grep -Fq 'python3 ./scripts/render-demo.py' "$root/Makefile"
grep -Fq 'libx264' "$root/scripts/render-demo.py"
grep -Fq 'yuv420p' "$root/scripts/render-demo.py"
grep -Fq 'docs/assets/qemu-m8-demo-poster.png' "$root/README.md"
grep -Fq 'docs/assets/qemu-m8-demo.mp4' "$root/README.md"
grep -Fq 'docs/assets/qemu-m8-demo.gif' "$root/README.md"
grep -Fq 'docs/assets/qemu-m8-demo-evidence.json' "$root/README.md"
grep -Fq 'docs/qemu-demo.md' "$root/README.md"
test -s "$root/docs/assets/qemu-m8-demo-poster.png"
test -s "$root/docs/assets/qemu-m8-demo.mp4"
test -s "$root/docs/assets/qemu-m8-demo.gif"
test -s "$root/docs/assets/qemu-m8-demo-evidence.json"
grep -Fq 'curl ffmpeg' "$workflow"
grep -Fq 'python3 scripts/render-demo.py' "$workflow"
grep -Fq 'out/m8/demo/qemu-m8-demo.mp4' "$workflow"
grep -Fq 'out/m8/demo/qemu-m8-demo.gif' "$workflow"
grep -Fq 'out/m8/demo/qemu-m8-demo-poster.png' "$workflow"
grep -Fq 'out/m8/demo/qemu-m8-demo-evidence.json' "$workflow"

echo 'PASS: validated M8 demo rendering contracts'
