#!/usr/bin/env bash
set -eu

# Runs the cached out/m8 build only after verifying that it belongs to this
# worktree and dependency set. QS_ALLOW_STALE_ARTIFACT=1 permits an intentional
# old build and labels the run with the identity that actually produced it.
root=${QS_ROOT:-$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)}
export QS_ROOT=$root QS_STAGE=m8
manifest=$root/out/m8/manifest.json

if [ "$#" -gt 0 ]; then
  echo "error: $0 takes no arguments" >&2
  exit 2
fi

build_identity() {
  python3 -c '
import json, sys
try:
    data = json.load(open(sys.argv[1]))
except (OSError, ValueError):
    print("unknown (no manifest)")
else:
    print("%s%s" % (data.get("revision", "unknown")[:12],
                    " (dirty worktree)" if data.get("dirty") else ""))
' "$manifest"
}

if [ "${QS_ALLOW_STALE_ARTIFACT:-0}" = 1 ]; then
  if ! "$root/scripts/m8-manifest.sh" verify; then
    echo "warning: cached M8 artifact does not match this worktree" >&2
    echo "warning: evidence from this run belongs to the cached build, not current HEAD" >&2
  fi
else
  "$root/scripts/m8-manifest.sh" verify
fi

# Every run carries the identity of the build it actually exercised, so the
# retained logs and rendered evidence cannot be read as current-source
# evidence for a cache built somewhere else.
export QS_M8_BUILD_IDENTITY=$(build_identity)
echo "QS:M8_BUILD_IDENTITY:$QS_M8_BUILD_IDENTITY"
exec sudo -E "$root/scripts/m8-smoke.sh"
