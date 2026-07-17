#!/usr/bin/env bash
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

mkdir -p "$tmp/bin"
cat > "$tmp/os-release" <<'EOF'
ID=ubuntu
VERSION_ID=26.04
EOF

for cmd in git make gcc riscv64-unknown-elf-gcc dtc qemu-system-riscv64 python3 ip tcpdump curl sha256sum; do
  printf '#!/bin/sh\nexit 0\n' > "$tmp/bin/$cmd"
  chmod +x "$tmp/bin/$cmd"
done

PATH="$tmp/bin:$PATH" \
QS_OS_RELEASE="$tmp/os-release" \
"$root/scripts/check-env.sh" || fail "Ubuntu 26.04 with all commands should pass"

mv "$tmp/bin/qemu-system-riscv64" "$tmp/qemu-system-riscv64"
if PATH="$tmp/bin" QS_OS_RELEASE="$tmp/os-release" /bin/bash "$root/scripts/check-env.sh" >/dev/null 2>"$tmp/qemu.err"; then
  fail "missing qemu-system-riscv64 should fail"
fi
grep -q 'missing: qemu-system-riscv64' "$tmp/qemu.err" || fail "missing-QEMU error should name the command"
mv "$tmp/qemu-system-riscv64" "$tmp/bin/qemu-system-riscv64"

sed 's/26.04/22.04/' "$tmp/os-release" > "$tmp/os-release-old"
if PATH="$tmp/bin:$PATH" QS_OS_RELEASE="$tmp/os-release-old" "$root/scripts/check-env.sh" 2>"$tmp/old.err"; then
  fail "Ubuntu 22.04 should be rejected"
fi
grep -q '24.04 or 26.04' "$tmp/old.err" || fail "unsupported-version error is unclear"

mkdir -p "$tmp/project/third_party" "$tmp/project/out/downloads"
printf 'fatfs-test-archive\n' > "$tmp/project/out/downloads/ff15.zip"
fatfs_hash=$(sha256sum "$tmp/project/out/downloads/ff15.zip" | cut -d' ' -f1)
cat > "$tmp/project/third_party/fatfs.lock" <<EOF
version=R0.15
url=https://example.invalid/ff15.zip
sha256=$fatfs_hash
EOF

QS_ROOT="$tmp/project" "$root/scripts/fetch-fatfs.sh" --check || fail "valid FatFs archive should pass"
printf 'corrupt\n' >> "$tmp/project/out/downloads/ff15.zip"
if QS_ROOT="$tmp/project" "$root/scripts/fetch-fatfs.sh" --check >/dev/null 2>&1; then
  fail "corrupt FatFs archive should fail"
fi

mkdir -p "$tmp/empty-repo"
git -C "$tmp/empty-repo" init -q
if QS_ROOT="$tmp/empty-repo" "$root/scripts/check-sources.sh" >"$tmp/sources.out" 2>"$tmp/sources.err"; then
  fail "missing submodules should fail"
fi
grep -q 'third_party/qemu' "$tmp/sources.err" || fail "missing-submodule error should name qemu"

mkdir -p "$tmp/gitlink-repo/third_party/qemu"
git -C "$tmp/gitlink-repo" init -q
git -C "$tmp/gitlink-repo" update-index --add --cacheinfo \
  160000,f7f686b61cf7ee142c9264d2e04ac2c6a96d37f8,third_party/qemu
if QS_ROOT="$tmp/gitlink-repo" "$root/scripts/check-sources.sh" >"$tmp/gitlink.out" 2>"$tmp/gitlink.err"; then
  fail "uninitialized gitlink should fail"
fi
grep -q 'third_party/qemu is at uninitialized' "$tmp/gitlink.err" || \
  fail "uninitialized-submodule error should be explicit"

if grep -q -- '--recursive' "$root/Makefile"; then
  fail "deps should initialize only direct project submodules"
fi

echo "PASS: M0 script behavior"
