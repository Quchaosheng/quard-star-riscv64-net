#!/usr/bin/env bash
set -eu

os_release=${QS_OS_RELEASE:-/etc/os-release}
if [ ! -r "$os_release" ]; then
  echo "error: $os_release is unavailable" >&2
  exit 1
fi

. "$os_release"
if [ "${ID:-}" != ubuntu ]; then
  echo "error: Ubuntu is required; found ${ID:-unknown} ${VERSION_ID:-unknown}" >&2
  exit 1
fi

case "${VERSION_ID:-}" in
  24.04|26.04) ;;
  *)
    echo "error: Ubuntu 24.04 or 26.04 LTS is required; found ${VERSION_ID:-unknown}" >&2
    exit 1
    ;;
esac

missing=0
for cmd in git make gcc riscv64-unknown-elf-gcc dtc qemu-system-riscv64 ninja meson pkg-config python3 ip tcpdump curl sha256sum; do
  if ! command -v "$cmd" >/dev/null 2>&1; then
    echo "missing: $cmd" >&2
    missing=1
  fi
done
exit "$missing"
