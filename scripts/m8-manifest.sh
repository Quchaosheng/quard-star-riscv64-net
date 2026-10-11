#!/usr/bin/env bash
set -eu

# Records and verifies the identity of the inputs that produced out/m8, so that
# `make run` proves a cached artifact belongs to the current worktree instead
# of assuming it.
#
#   scripts/m8-manifest.sh write   write out/m8/manifest.json for this worktree
#   scripts/m8-manifest.sh verify  fail unless the cached artifact matches it
#
# Tree identity covers tracked content as it exists on disk plus untracked,
# non-ignored files, so uncommitted source edits invalidate a cached run.
# Ignored paths (out/, kernel/*.bin, trusted/build/) are build products, not
# inputs, and are excluded.
root=${QS_ROOT:-$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)}
stage=${QS_STAGE:-m8}
out=$root/out/$stage
manifest=$out/manifest.json

case "${1:-}" in
  write | verify) action=$1 ;;
  *) echo "usage: $0 write|verify" >&2; exit 2 ;;
esac

# Every tracked file's on-disk content plus untracked non-ignored files. Submodule
# gitlinks are recorded separately by their commit id, so their contents (which
# are not this repository's source) are excluded from the tree digest.
tree_digest() {
  # --cached lists submodule gitlinks as directory paths, so drop entries that
  # are not regular files; submodule identity is recorded separately.
  git -C "$root" ls-files -z --cached --others --exclude-standard | \
    sort -z | while IFS= read -r -d '' path; do
      [ -f "$root/$path" ] && printf '%s\0' "$path"
    done | (cd "$root" && xargs -0 -r sha256sum --) | \
    sha256sum | cut -d' ' -f1
}

collect() {
  {
    printf 'REVISION %s\n' "$(git -C "$root" rev-parse HEAD)"
    if [ -n "$(git -C "$root" status --porcelain)" ]; then
      printf 'DIRTY true\n'
    else
      printf 'DIRTY false\n'
    fi
    printf 'TREE %s\n' "$(tree_digest)"
    printf 'FATFS_LOCK %s\n' \
      "$(sha256sum "$root/third_party/fatfs.lock" 2>/dev/null | cut -c1-16)"
    printf 'CROSS_CC %s\n' \
      "$(riscv64-unknown-elf-gcc --version 2>/dev/null | head -1 || echo unavailable)"
    printf 'DTC %s\n' "$(dtc --version 2>/dev/null | head -1 || echo unavailable)"
    for dep in third_party/qemu third_party/opensbi third_party/freertos \
      third_party/dtc third_party/nanoprintf; do
      printf 'DEP %s %s %s\n' "$dep" \
        "$(git -C "$root" ls-files -s -- "$dep" | awk '{print $2}')" \
        "$(git -C "$dep" rev-parse HEAD 2>/dev/null || echo uninitialized)"
    done
    for patch in "$root"/patches/*/*.patch; do
      [ -e "$patch" ] || continue
      printf 'PATCH %s %s\n' "${patch#"$root"/}" \
        "$(sha256sum "$patch" | cut -c1-16)"
    done
    for artifact in fw/fw.bin disk/disk.img qemu/qemu-system-riscv64; do
      printf 'ARTIFACT %s %s\n' "$artifact" \
        "$(sha256sum "$out/$artifact" 2>/dev/null | cut -c1-64)"
    done
  }
}

if [ "$action" = write ]; then
  for artifact in fw/fw.bin disk/disk.img qemu/qemu-system-riscv64; do
    if [ ! -f "$out/$artifact" ]; then
      echo "error: missing $out/$artifact; build $stage first" >&2
      exit 1
    fi
  done
fi

identity=$(collect)
python3 - "$manifest" "$action" "$identity" <<'PY'
import json
import pathlib
import sys

manifest_path, action, identity = sys.argv[1:]

fields = {}
dependencies = {}
patches = {}
artifacts = {}
for line in identity.splitlines():
    parts = line.split(" ", 2)
    if parts[0] == "DEP":
        dependencies[parts[1]] = parts[2]
    elif parts[0] == "PATCH":
        patches[parts[1]] = parts[2]
    elif parts[0] == "ARTIFACT":
        artifacts[parts[1]] = parts[2]
    else:
        fields[parts[0]] = parts[1] if len(parts) > 1 else ""

document = {
    "revision": fields.get("REVISION", ""),
    "dirty": fields.get("DIRTY") == "true",
    "tree_sha256": fields.get("TREE", ""),
    "dependencies": dependencies,
    "patches": patches,
    "fatfs_lock_sha256": fields.get("FATFS_LOCK", ""),
    "toolchain": {
        "cross_prefix": fields.get("CROSS_CC", ""),
        "dtc": fields.get("DTC", ""),
    },
    "artifacts": artifacts,
}

if action == "write":
    pathlib.Path(manifest_path).write_text(
        json.dumps(document, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(f"wrote {manifest_path}", file=sys.stderr)
    raise SystemExit(0)

path = pathlib.Path(manifest_path)
if not path.is_file():
    print(f"error: {manifest_path} is missing; rebuild with 'make m8-build'",
          file=sys.stderr)
    raise SystemExit(1)
recorded = json.loads(path.read_text(encoding="utf-8"))

reasons = []
if recorded.get("revision") != document["revision"]:
    reasons.append(
        "manifest was built at %s, current HEAD is %s"
        % (str(recorded.get("revision"))[:12], document["revision"][:12]))
if recorded.get("tree_sha256") != document["tree_sha256"]:
    if document["dirty"]:
        reasons.append("the worktree has uncommitted changes since the cached build")
    else:
        reasons.append("tracked tree content no longer matches the cached build")
if recorded.get("dependencies") != dependencies:
    for name, value in dependencies.items():
        if recorded.get("dependencies", {}).get(name) != value:
            reasons.append(
                "dependency %s changed: manifest %s, now %s"
                % (name, recorded.get("dependencies", {}).get(name), value))
if recorded.get("patches") != patches:
    for name, value in patches.items():
        if recorded.get("patches", {}).get(name) != value:
            reasons.append("patch %s changed since the cached build" % name)
if recorded.get("fatfs_lock_sha256") != document["fatfs_lock_sha256"]:
    reasons.append("third_party/fatfs.lock changed since the cached build")
if recorded.get("toolchain") != document["toolchain"]:
    reasons.append("the toolchain reports a different version than the cached build")
if recorded.get("artifacts") != artifacts:
    for name, value in artifacts.items():
        if recorded.get("artifacts", {}).get(name) != value:
            reasons.append("artifact %s does not match its recorded digest" % name)

if reasons:
    print("error: cached M8 artifact does not match this worktree:", file=sys.stderr)
    for reason in reasons:
        print("  - %s" % reason, file=sys.stderr)
    print("rebuild with 'make m8-build', or re-run the cached artifact with "
          "QS_ALLOW_STALE_ARTIFACT=1 to label its evidence with the build "
          "identity that produced it.", file=sys.stderr)
    raise SystemExit(1)

print("verified cached M8 artifact from %s%s"
      % (document["revision"][:12],
         " (dirty worktree)" if document["dirty"] else ""), file=sys.stderr)
PY
