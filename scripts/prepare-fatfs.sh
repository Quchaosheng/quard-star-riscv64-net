#!/usr/bin/env bash
set -eu

root=${QS_ROOT:-$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)}
archive=$root/out/downloads/ff15.zip
target=$root/out/deps/fatfs
config=$root/kernel/fs/fatfs/ffconf.h
lock=$root/third_party/fatfs.lock
archive_prefix=$(sed -n 's/^archive_prefix=//p' "$lock")

case "$archive_prefix" in
  ""|*/ ) ;;
  * ) echo "error: FatFs archive_prefix must be empty or end with /" >&2; exit 1 ;;
esac
case "$archive_prefix" in
  /*|*../* ) echo "error: unsafe FatFs archive_prefix" >&2; exit 1 ;;
esac

"$root/scripts/fetch-fatfs.sh"
"$root/scripts/fetch-fatfs.sh" --check
[ -f "$config" ] || {
  echo "error: missing $config" >&2
  exit 1
}

rm -rf "$target"
python3 - "$archive" "$target" "$archive_prefix" <<'PY'
import pathlib
import sys
import zipfile

archive_path = pathlib.Path(sys.argv[1])
target = pathlib.Path(sys.argv[2])
prefix = sys.argv[3]
members = (
    "source/ff.c",
    "source/ff.h",
    "source/diskio.h",
    "LICENSE.txt",
)

with zipfile.ZipFile(archive_path) as archive:
    names = set(archive.namelist())
    missing = [name for name in members if prefix + name not in names]
    if missing:
        raise SystemExit("error: FatFs archive is missing " + ", ".join(missing))
    for name in members:
        destination = target / name
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_bytes(archive.read(prefix + name))
PY

cp "$config" "$target/source/ffconf.h"
