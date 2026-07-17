#!/usr/bin/env bash
set -eu

root=${QS_ROOT:-$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)}
lock=$root/third_party/fatfs.lock
archive=$root/out/downloads/ff15.zip
part=$archive.part

read_value() {
  sed -n "s/^$1=//p" "$lock"
}

if [ ! -f "$lock" ]; then
  echo "error: missing $lock" >&2
  exit 1
fi

url=$(read_value url)
sha256=$(read_value sha256)
if ! printf '%s\n' "$sha256" | grep -Eq '^[0-9a-f]{64}$'; then
  echo "error: invalid FatFs SHA-256 lock" >&2
  exit 1
fi

verify() {
  [ -f "$1" ] && printf '%s  %s\n' "$sha256" "$1" | sha256sum -c - >/dev/null
}

if [ "${1:-}" = --check ]; then
  verify "$archive"
  exit $?
fi

mkdir -p "$(dirname -- "$archive")"
if verify "$archive"; then
  exit 0
fi

rm -f "$part"
curl --fail --location --retry 5 --output "$part" "$url"
verify "$part"
mv -f "$part" "$archive"
