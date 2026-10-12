#!/usr/bin/env bash
set -eu

# Fixture behavior checks for cached-artifact provenance. Each case builds a
# throwaway repository, fabricates the three cached artifacts, writes a real
# manifest with scripts/m8-manifest.sh, then mutates one input and asserts that
# verification rejects the cache with a named reason. Nothing here reads source
# text to decide the outcome.

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
manifest_script=$root/scripts/m8-manifest.sh

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

# A miniature project with the same identity inputs the real repository has:
# tracked sources, pinned patch and lock files, and out/ as a build directory.
new_fixture() {
  fixture=$tmp/$1
  rm -rf "$fixture"
  mkdir -p "$fixture/out/m8/qemu" "$fixture/out/m8/fw" "$fixture/out/m8/disk"
  mkdir -p "$fixture/platform/quard-star/dts" "$fixture/patches/qemu" \
    "$fixture/third_party"
  printf '/out/\n' > "$fixture/.gitignore"
  printf 'sbi dts\n' > "$fixture/platform/quard-star/dts/quard_star_sbi.dts"
  printf 'kernel source\n' > "$fixture/platform/kernel.c"
  printf 'version=R0.15\nsha256=deadbeef\n' > "$fixture/third_party/fatfs.lock"
  printf 'diff --git a/x b/x\n' > "$fixture/patches/qemu/0001-add-machine.patch"
  git -C "$fixture" init -q
  git -C "$fixture" add -A
  git -C "$fixture" -c user.name=fixture -c user.email=fixture@test \
    commit -q -m fixture-base
  head -c 128 /dev/urandom > "$fixture/out/m8/fw/fw.bin"
  head -c 128 /dev/urandom > "$fixture/out/m8/disk/disk.img"
  head -c 128 /dev/urandom > "$fixture/out/m8/qemu/qemu-system-riscv64"
}

verify() {
  QS_ROOT=$fixture "$manifest_script" verify
}

write_manifest() {
  QS_ROOT=$fixture "$manifest_script" write >/dev/null 2>&1
}

write_then_expect_verify_ok() {
  write_manifest || fail "$1: writing a manifest for a complete cache must succeed"
  verify >/dev/null 2>&1 || fail "$1: a freshly written manifest must verify"
}

# A cache built from the current tree must verify without a rebuild.
new_fixture match
write_then_expect_verify_ok "matching build"

# A write must fail when an artifact is absent, so an incomplete cache can never
# be labelled as verified.
new_fixture incomplete
rm -f "$fixture/out/m8/disk/disk.img"
if write_manifest; then
  fail "writing a manifest without disk.img must fail"
fi
if QS_ROOT=$fixture "$manifest_script" write 2>&1 | \
  grep -q 'missing'; then
  :
else
  fail "the incomplete-cache error should name the missing artifact"
fi

# Illegal state: a missing manifest cannot be treated as a match.
new_fixture missing-manifest
if verify >/dev/null 2>&1; then
  fail "a missing manifest must invalidate the cached run"
fi
verify 2>&1 | grep -q 'missing' || \
  fail "the missing-manifest error should give a rebuild instruction"

# Untracked source edits must invalidate, even though HEAD is unchanged.
new_fixture dirty-untracked
write_then_expect_verify_ok "dirty-untracked baseline"
printf 'local edit\n' >> "$fixture/platform/kernel.c"
if verify >/dev/null 2>&1; then
  fail "uncommitted source edits must invalidate the cached run"
fi
verify 2>&1 | grep -qi 'uncommitted' || \
  fail "the dirty-worktree reason should be explicit"
git -C "$fixture" checkout -q -- platform/kernel.c
verify >/dev/null 2>&1 || fail "reverting the edit must restore the match"

# A brand-new untracked source file is an input change too.
new_fixture dirty-new-file
write_then_expect_verify_ok "dirty-new-file baseline"
printf 'new source\n' > "$fixture/platform/extra.c"
if verify >/dev/null 2>&1; then
  fail "a new untracked source file must invalidate the cached run"
fi

# A different commit must invalidate.
new_fixture other-commit
write_then_expect_verify_ok "other-commit baseline"
git -C "$fixture" -c user.name=fixture -c user.email=fixture@test \
  commit -q --allow-empty -m later
if verify >/dev/null 2>&1; then
  fail "a different commit must invalidate the cached run"
fi
verify 2>&1 | grep -q 'current HEAD is' || \
  fail "the commit-mismatch reason should name both revisions"

# A replaced artifact must invalidate, even when the tree is untouched.
for artifact in fw/fw.bin disk/disk.img qemu/qemu-system-riscv64; do
  new_fixture "replaced-$(basename "$artifact")"
  write_then_expect_verify_ok "replaced-$artifact baseline"
  head -c 128 /dev/urandom > "$fixture/out/m8/$artifact"
  if verify >/dev/null 2>&1; then
    fail "a replaced $artifact must invalidate the cached run"
  fi
  verify 2>&1 | grep -q "artifact $artifact" || \
    fail "the replaced-artifact reason should name $artifact"
done

# A dependency lock change must invalidate.
new_fixture lock-change
write_then_expect_verify_ok "lock-change baseline"
printf 'version=R0.16\nsha256=cafe\n' > "$fixture/third_party/fatfs.lock"
if verify >/dev/null 2>&1; then
  fail "a dependency lock change must invalidate the cached run"
fi
verify 2>&1 | grep -q 'fatfs.lock changed' || \
  fail "the lock-change reason should name fatfs.lock"

# A patch change must invalidate.
new_fixture patch-change
write_then_expect_verify_ok "patch-change baseline"
printf 'extra hunk\n' >> "$fixture/patches/qemu/0001-add-machine.patch"
if verify >/dev/null 2>&1; then
  fail "a patch change must invalidate the cached run"
fi
verify 2>&1 | grep -q 'patch patches/qemu/0001-add-machine.patch changed' || \
  fail "the patch-change reason should name the patch"

# A second identical write must reproduce the same manifest, so a matching
# build is stable rather than accidentally invalidating itself.
new_fixture reproducible
write_then_expect_verify_ok "reproducible baseline"
cp "$fixture/out/m8/manifest.json" "$tmp/manifest.first"
write_manifest || fail "rewriting an unchanged manifest must succeed"
cmp -s "$fixture/out/m8/manifest.json" "$tmp/manifest.first" || \
  fail "an unchanged worktree must produce an identical manifest"
verify >/dev/null 2>&1 || fail "the rewritten manifest must still verify"

# The manifest must actually carry the identities the issue requires.
python3 - "$fixture/out/m8/manifest.json" <<'PY' || fail "manifest is missing required identities"
import json
import sys

data = json.load(open(sys.argv[1]))
assert len(data["revision"]) == 40, "revision must be a full commit id"
assert len(data["tree_sha256"]) == 64, "tree identity must be a digest"
assert isinstance(data["dirty"], bool), "dirty must be recorded"
assert data["dependencies"], "dependency identities must be recorded"
assert data["patches"], "patch identities must be recorded"
assert data["fatfs_lock_sha256"], "the dependency lock digest must be recorded"
assert data["toolchain"], "toolchain identity must be recorded"
for name in ("fw/fw.bin", "disk/disk.img", "qemu/qemu-system-riscv64"):
    assert len(data["artifacts"][name]) == 64, f"{name} needs an artifact digest"
PY

echo 'PASS: M8 cached artifact provenance behavior'
