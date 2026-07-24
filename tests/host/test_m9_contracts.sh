#!/usr/bin/env bash
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
workflow=${QS_M9_HOST_WORKFLOW:-$root/.github/workflows/host-tests.yml}
smoke_workflow=${QS_M9_SMOKE_WORKFLOW:-$root/.github/workflows/m8-smoke.yml}
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

collect_action_refs()
{
  sed -nE \
    -e 's/^[[:space:]]*-[[:space:]]*uses:[[:space:]]*["'"'"']?([^[:space:]#"'"'"']+)["'"'"']?.*$/\1/p' \
    -e 's/^[[:space:]]*uses:[[:space:]]*["'"'"']?([^[:space:]#"'"'"']+)["'"'"']?.*$/\1/p' \
    "$1" >"$2"
}

if [ "${QS_M9_CHECKOUT_FIXTURE:-0}" -eq 0 ]; then
  host_fixture=$tmp/host-tests.yml
  smoke_fixture=$tmp/m8-smoke.yml
  cp "$workflow" "$host_fixture"
  cp "$smoke_workflow" "$smoke_fixture"
  sed -i 's/actions\/checkout@v5/actions\/checkout@v4/' "$host_fixture"
  sed -i 's/actions\/checkout@v5/actions\/checkout@v4/' "$smoke_fixture"
  printf '%s\n' '# uses: actions/checkout@v5' >>"$host_fixture"
  printf '%s\n' '# uses: actions/checkout@v5' >>"$smoke_fixture"
  if QS_M9_ACTION_FIXTURE=1 QS_M9_SUBMODULE_FIXTURE=1 \
    QS_M9_CHECKOUT_FIXTURE=1 QS_M9_HOST_WORKFLOW="$host_fixture" \
    QS_M9_SMOKE_WORKFLOW="$smoke_workflow" "$0" >/dev/null 2>&1; then
    echo 'FAIL: host checkout comments must not satisfy the version contract' >&2
    exit 1
  fi
  if QS_M9_ACTION_FIXTURE=1 QS_M9_SUBMODULE_FIXTURE=1 \
    QS_M9_CHECKOUT_FIXTURE=1 QS_M9_HOST_WORKFLOW="$workflow" \
    QS_M9_SMOKE_WORKFLOW="$smoke_fixture" "$0" >/dev/null 2>&1; then
    echo 'FAIL: M8 checkout must use the required active version' >&2
    exit 1
  fi
fi

if [ "${QS_M9_SUBMODULE_FIXTURE:-0}" -eq 0 ]; then
  fixture=$tmp/m8-smoke.yml
  cp "$smoke_workflow" "$fixture"
  sed -i 's/submodules: true$/submodules: true # direct checkout/' "$fixture"
  QS_M9_ACTION_FIXTURE=1 QS_M9_SUBMODULE_FIXTURE=1 \
    QS_M9_SMOKE_WORKFLOW="$fixture" "$0" || {
    echo 'FAIL: submodules true must allow a trailing YAML comment' >&2
    exit 1
  }
fi

if [ "${QS_M9_ACTION_FIXTURE:-0}" -eq 0 ]; then
  fixture=$tmp/m8-smoke.yml
  cp "$smoke_workflow" "$fixture"
  cat >>"$fixture" <<'EOF'
# actions/cache@v6
# "actions/upload-artifact@v7"
      - name: "actions/cache@v6"
EOF
  QS_M9_ACTION_FIXTURE=1 QS_M9_SMOKE_WORKFLOW="$fixture" "$0" || {
    echo 'FAIL: action comments and quoted strings must not count as uses' >&2
    exit 1
  }
fi
grep -Fq 'runs-on: ubuntu-24.04' "$workflow"
if grep -Eq '^[[:space:]]+submodules:' "$workflow" ||
   grep -Eq '^[[:space:]]*(run:[[:space:]]*)?git[[:space:]]+submodule([[:space:]]|$)' \
     "$workflow"; then
  echo 'FAIL: host CI must not fetch build-only submodules' >&2
  exit 1
fi
submodules_values=$tmp/submodules-values
sed -nE \
  's/^[[:space:]]+submodules:[[:space:]]*(true|false)[[:space:]]*(#.*)?$/\1/ip' \
  "$smoke_workflow" >"$submodules_values"
if [ "$(wc -l <"$submodules_values")" -ne 1 ] ||
   ! grep -Eiq '^true$' "$submodules_values"; then
  echo 'FAIL: M8 CI must enable direct submodules checkout' >&2
  exit 1
fi
if grep -Fq 'submodules: recursive' "$workflow" "$smoke_workflow"; then
  echo 'FAIL: CI must initialize only direct project submodules' >&2
  exit 1
fi
grep -Fq 'run: make test-host' "$workflow"
host_action_refs=$tmp/host-action-refs
smoke_action_refs=$tmp/smoke-action-refs
collect_action_refs "$workflow" "$host_action_refs"
collect_action_refs "$smoke_workflow" "$smoke_action_refs"
host_checkout_refs=$(grep -Ec '^actions/checkout@' "$host_action_refs" || true)
host_checkout_v5_refs=$(grep -Fxc 'actions/checkout@v5' "$host_action_refs" || true)
smoke_checkout_refs=$(grep -Ec '^actions/checkout@' "$smoke_action_refs" || true)
smoke_checkout_v5_refs=$(grep -Fxc 'actions/checkout@v5' "$smoke_action_refs" || true)
if [ "$host_checkout_refs" -ne 1 ] || [ "$host_checkout_v5_refs" -ne 1 ] ||
   [ "$smoke_checkout_refs" -ne 1 ] || [ "$smoke_checkout_v5_refs" -ne 1 ]; then
  echo 'FAIL: CI workflows must use checkout v5 exactly once' >&2
  exit 1
fi
grep -Fq 'device-tree-compiler' "$workflow"
grep -Fq 'gcc-riscv64-unknown-elf' "$workflow"
grep -Fq 'workflow_dispatch:' "$smoke_workflow"
grep -Fq 'run: ./scripts/prepare-fatfs.sh' "$smoke_workflow"
grep -Fq 'run: make m8-build' "$smoke_workflow"
grep -Fq 'run: sudo -E make m8-smoke' "$smoke_workflow"
job_count=$(grep -Fc '  qemu-smoke:' "$smoke_workflow" || true)
if [ "$job_count" -ne 1 ]; then
  echo 'FAIL: M8 CI must define exactly one qemu-smoke job' >&2
  exit 1
fi
smoke_job=$tmp/qemu-smoke.yml
awk '
  $0 == "  qemu-smoke:" { found = 1 }
  found && /^  [^[:space:]#][^:]*:[[:space:]]*$/ && $0 != "  qemu-smoke:" { exit }
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
action_refs=$tmp/action-refs
collect_action_refs "$smoke_job" "$action_refs"
cache_refs=$(grep -Ec '^actions/cache@' "$action_refs" || true)
cache_v6_refs=$(grep -Fxc 'actions/cache@v6' "$action_refs" || true)
upload_refs=$(grep -Ec '^actions/upload-artifact@' "$action_refs" || true)
upload_v7_refs=$(grep -Fxc 'actions/upload-artifact@v7' "$action_refs" || true)
if [ "$cache_refs" -ne 1 ] || [ "$cache_v6_refs" -ne 1 ] ||
   [ "$upload_refs" -ne 1 ] || [ "$upload_v7_refs" -ne 1 ] ||
   grep -Eiq '^[[:space:]]+if:[[:space:]]*(false|\$\{\{[[:space:]]*false[[:space:]]*\}\})[[:space:]]*(#.*)?$' "$smoke_job"; then
  echo 'FAIL: qemu-smoke must use Node.js 24 cache and upload actions exactly once' >&2
  exit 1
fi
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
