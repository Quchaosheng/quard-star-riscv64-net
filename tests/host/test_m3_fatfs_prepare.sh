#!/usr/bin/env bash
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

[ -x "$root/scripts/prepare-fatfs.sh" ] || \
  fail "missing scripts/prepare-fatfs.sh"

project=$tmp/project
mkdir -p "$project/scripts" "$project/third_party" \
  "$project/kernel/fs/fatfs" "$project/out/downloads"
cp "$root/scripts/fetch-fatfs.sh" "$project/scripts/"
cp "$root/scripts/prepare-fatfs.sh" "$project/scripts/"
printf '#define FFCONF_DEF 80286\n' > "$project/kernel/fs/fatfs/ffconf.h"

python3 - "$project/out/downloads/ff15.zip" <<'PY'
import sys
import zipfile

members = {
    "source/ff.c": "ff source\n",
    "source/ff.h": "ff header\n",
    "source/diskio.h": "disk header\n",
    "documents/documents.html": "not allowlisted\n",
    "LICENSE.txt": "FatFs license\n",
}
with zipfile.ZipFile(sys.argv[1], "w") as archive:
    for name, contents in members.items():
        archive.writestr(name, contents)
PY

hash=$(sha256sum "$project/out/downloads/ff15.zip" | cut -d' ' -f1)
cat > "$project/third_party/fatfs.lock" <<EOF
version=R0.15
url=https://example.invalid/ff15.zip
sha256=$hash
EOF

QS_ROOT="$project" "$project/scripts/prepare-fatfs.sh" || \
  fail "valid allowlist archive should prepare"

actual=$(cd "$project/out/deps/fatfs" && find . -type f -printf '%P\n' | sort)
expected=$(printf '%s\n' LICENSE.txt source/diskio.h source/ff.c source/ff.h source/ffconf.h | sort)
[ "$actual" = "$expected" ] || {
  printf 'expected:\n%s\nactual:\n%s\n' "$expected" "$actual" >&2
  fail "prepared tree must contain only the allowlist"
}
cmp "$project/kernel/fs/fatfs/ffconf.h" \
  "$project/out/deps/fatfs/source/ffconf.h" >/dev/null || \
  fail "first-party ffconf.h must replace archive configuration"

printf 'keep-before-verify\n' > "$project/out/deps/fatfs/sentinel"
printf 'corrupt\n' >> "$project/out/downloads/ff15.zip"
if QS_ROOT="$project" "$project/scripts/prepare-fatfs.sh" >/dev/null 2>&1; then
  fail "corrupt archive should fail"
fi
[ -f "$project/out/deps/fatfs/sentinel" ] || \
  fail "archive verification must happen before clearing extracted output"

echo "PASS: M3 FatFs preparation behavior"
