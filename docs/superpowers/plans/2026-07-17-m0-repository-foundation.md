# M0 Repository Foundation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Establish a clean, reproducible repository foundation with verified authorship, fixed source baselines, licensed third-party dependencies, and no imported legacy Git history.

**Architecture:** Keep first-party code, third-party source, and patches in separate top-level directories. Git-capable upstream dependencies are pinned as shallow submodules; FatFs is fetched from the official R0.15 archive and locked by SHA-256. M0 adds no kernel functionality and finishes with machine-runnable repository checks.

**Tech Stack:** Git, Git submodules, Bash, PowerShell only for Windows bootstrap, SHA-256, Markdown, GitHub Actions-ready Linux layout.

---

### Task 1: Repository Identity and Remote

**Files:**
- Modify: `.git/config`

- [ ] **Step 1: Rename the unborn branch and add the target remote**

Run:

```bash
git branch -m main
git remote add origin https://github.com/Quchaosheng/quard-star-riscv64-net.git
```

Expected: `git branch --show-current` prints `main`, and `git remote get-url origin` prints the target GitHub URL.

- [ ] **Step 2: Configure repository-local authorship**

Run inside WSL from the repository:

```bash
git config --local user.name "Quchaosheng"
read -rp "GitHub verified email: " NEW_GIT_EMAIL
test -n "$NEW_GIT_EMAIL"
git config --local user.email "$NEW_GIT_EMAIL"
unset NEW_GIT_EMAIL
```

Expected: `git config --local --get user.name` prints `Quchaosheng`; the email printed by `git config --local --get user.email` is verified on the target GitHub account.

- [ ] **Step 3: Verify no global identity was changed**

Run:

```bash
git config --show-origin --get user.name
git config --show-origin --get user.email
```

Expected: the effective entries for this repository point to `.git/config`.

### Task 2: Root Repository Metadata

**Files:**
- Create: `.gitattributes`
- Create: `.gitignore`
- Create: `LICENSE`
- Modify: `README.md`

- [ ] **Step 1: Add line-ending rules**

Create `.gitattributes`:

```gitattributes
* text=auto
*.c text eol=lf
*.h text eol=lf
*.S text eol=lf
*.s text eol=lf
*.ld text eol=lf
*.lds text eol=lf
*.dts text eol=lf
*.dtsi text eol=lf
*.sh text eol=lf
Makefile text eol=lf
*.mk text eol=lf
*.md text eol=lf
*.json text eol=lf
*.ps1 text eol=crlf
*.bat text eol=crlf
```

- [ ] **Step 2: Ignore only generated and local files**

Create `.gitignore`:

```gitignore
/out/
/build/
/.cache/
/.vscode/
*.o
*.a
*.elf
*.bin
*.hex
*.map
*.lst
*.log
*.img
*.pcap
*.swp
*~
```

- [ ] **Step 3: Add the MIT license**

Create `LICENSE`:

```text
MIT License

Copyright (c) 2026 Quchaosheng

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

- [ ] **Step 4: Check README boundaries**

Verify `README.md` links to `docs/quard-star-riscv64-net-design.md`, describes the project as design-stage, and contains the short rCore acknowledgement approved by the owner. Do not add build commands before they exist.

- [ ] **Step 5: Verify metadata**

Run:

```bash
git check-attr eol -- README.md kernel.c script.sh
git check-ignore -v out/test.bin build/test.o
```

Expected: Markdown and shell/C examples resolve to LF rules; both generated paths are ignored.

### Task 3: Source and License Registry

**Files:**
- Create: `THIRD_PARTY.md`
- Create: `docs/source-migration.md`
- Create: `third_party/fatfs.lock`

- [ ] **Step 1: Record first-party migration sources**

Create `docs/source-migration.md` with these immutable baselines:

```markdown
# Source Migration

| Source | Commit | Included paths |
|---|---|---|
| Quchaosheng/quard-star-riscv64-kernel | 641f42560999ab00ad7ba01169cb2b3d723d8c48 | boot, dts, os, trusted platform code, quard-star QEMU/OpenSBI changes |
| Quchaosheng/tiny-tcpip-stack | 32e4988e2d482ad3ee406e36b5adbd84a63c8e9e | code/pc/src/net/net, code/pc/src/net/src, selected code/pc/src/app modules |

`code/src/net`, `code/x86os-with-net`, and `chapter` are excluded as protocol-core sources.
```

- [ ] **Step 2: Add the third-party registry**

Create `THIRD_PARTY.md` with one row for QEMU, OpenSBI, FreeRTOS-Kernel, dtc/libfdt, FatFs, and nanoprintf. Each row must contain upstream URL, fixed tag, resolved commit or archive SHA-256, license, purpose, modification status, and patch location.

- [ ] **Step 3: Lock the official FatFs archive**

Download the official archive twice and generate the lock from the actual matching hashes:

```bash
mkdir -p out/bootstrap third_party
url=https://elm-chan.org/fsw/ff/arc/ff15.zip
curl --fail --location --retry 5 --output out/bootstrap/ff15-a.zip "$url"
curl --fail --location --retry 5 --output out/bootstrap/ff15-b.zip "$url"
hash_a=$(sha256sum out/bootstrap/ff15-a.zip | cut -d' ' -f1)
hash_b=$(sha256sum out/bootstrap/ff15-b.zip | cut -d' ' -f1)
test "$hash_a" = "$hash_b"
printf 'version=R0.15\nurl=%s\nsha256=%s\n' "$url" "$hash_a" > third_party/fatfs.lock
```

Expected: a second download produces the same SHA-256 before any source is extracted.

### Task 4: Fixed Third-Party Dependencies

**Files:**
- Create: `.gitmodules`
- Create: `third_party/qemu/` gitlink
- Create: `third_party/opensbi/` gitlink
- Create: `third_party/freertos/` gitlink
- Create: `third_party/dtc/` gitlink
- Create: `third_party/nanoprintf/` gitlink

- [ ] **Step 1: Add shallow, tagged submodules**

Run:

```bash
git submodule add --depth 1 -b v8.0.2 https://github.com/qemu/qemu.git third_party/qemu
git submodule add --depth 1 -b v1.2 https://github.com/riscv-software-src/opensbi.git third_party/opensbi
git submodule add --depth 1 -b V10.5.1 https://github.com/FreeRTOS/FreeRTOS-Kernel.git third_party/freertos
git submodule add --depth 1 -b v1.7.0 https://git.kernel.org/pub/scm/utils/dtc/dtc.git third_party/dtc
git submodule add --depth 1 https://github.com/charlesnicholson/nanoprintf.git third_party/nanoprintf
```

Expected: every path is a gitlink and `git submodule status` prints a concrete commit for all five dependencies.

- [ ] **Step 2: Prevent accidental branch drift**

Remove all `branch = ...` entries from `.gitmodules` after the gitlinks are created, then add `shallow = true` to each submodule section. The superproject gitlink remains the only selected revision.

- [ ] **Step 3: Verify licenses exist**

Run a script that checks the pinned trees for QEMU `COPYING`, OpenSBI `COPYING.BSD`, FreeRTOS `LICENSE.md`, dtc `GPL`, and nanoprintf `LICENSE`. Fail M0 if any expected license file is missing.

### Task 5: Reproducible Dependency Checks

**Files:**
- Create: `scripts/check-env.sh`
- Create: `scripts/check-sources.sh`
- Create: `scripts/fetch-fatfs.sh`
- Create: `Makefile`

- [ ] **Step 1: Write a read-only environment check**

Create `scripts/check-env.sh`:

```bash
#!/usr/bin/env bash
set -eu

if [ ! -r /etc/os-release ]; then
  echo "error: /etc/os-release is unavailable" >&2
  exit 1
fi

. /etc/os-release
if [ "$ID" != ubuntu ]; then
  echo "error: Ubuntu is required; found $ID $VERSION_ID" >&2
  exit 1
fi
case "$VERSION_ID" in
  24.04|26.04) ;;
  *) echo "error: Ubuntu 24.04 or 26.04 LTS is required; found $VERSION_ID" >&2; exit 1 ;;
esac

missing=0
for cmd in git make gcc riscv64-unknown-elf-gcc dtc python3 ip tcpdump curl sha256sum; do
  if ! command -v "$cmd" >/dev/null 2>&1; then
    echo "missing: $cmd" >&2
    missing=1
  fi
done
exit "$missing"
```

- [ ] **Step 2: Write the FatFs fetcher**

Create `scripts/fetch-fatfs.sh`:

```bash
#!/usr/bin/env bash
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
lock=$root/third_party/fatfs.lock
archive=$root/out/downloads/ff15.zip
part=$archive.part

read_value() {
  sed -n "s/^$1=//p" "$lock"
}

url=$(read_value url)
sha256=$(read_value sha256)
if ! printf '%s\n' "$sha256" | grep -Eq '^[0-9a-f]{64}$'; then
  echo "error: invalid FatFs SHA-256 lock" >&2
  exit 1
fi

verify() {
  printf '%s  %s\n' "$sha256" "$1" | sha256sum -c - >/dev/null
}

if [ "${1:-}" = --check ]; then
  verify "$archive"
  exit 0
fi

mkdir -p "$(dirname -- "$archive")"
if [ -f "$archive" ] && verify "$archive"; then
  exit 0
fi

curl --fail --location --retry 5 --output "$part" "$url"
verify "$part"
mv -f "$part" "$archive"
```

Extraction is deferred until the FatFs port is introduced; M0 only stores the verified archive under ignored `out/`.

- [ ] **Step 3: Write the source verifier**

Create `scripts/check-sources.sh`:

```bash
#!/usr/bin/env bash
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$root"

for path in qemu opensbi freertos dtc nanoprintf; do
  submodule=third_party/$path
  expected=$(git ls-files -s "$submodule" | awk '{print $2}')
  [ -n "$expected" ]
  actual=$(git -C "$submodule" rev-parse HEAD)
  if [ "$expected" != "$actual" ]; then
    echo "error: $submodule is at $actual, expected $expected" >&2
    exit 1
  fi
done

test -f third_party/qemu/COPYING
test -f third_party/opensbi/COPYING.BSD
test -f third_party/freertos/LICENSE.md
test -f third_party/dtc/GPL
test -f third_party/nanoprintf/LICENSE

for series in patches/*/series; do
  [ -e "$series" ] || continue
  base=${series%/series}
  while IFS= read -r patch; do
    [ -n "$patch" ] || continue
    case "$patch" in \#*) continue ;; esac
    git -C "third_party/$(basename "$base")" apply --check "$root/$base/$patch"
  done < "$series"
done

if [ -f out/downloads/ff15.zip ]; then
  scripts/fetch-fatfs.sh --check
fi
```

- [ ] **Step 4: Add stable top-level commands**

Create a root `Makefile` exposing only:

```make
.PHONY: check-env deps check-sources

check-env:
	./scripts/check-env.sh

deps:
	git submodule update --init --recursive --depth 1
	./scripts/fetch-fatfs.sh

check-sources:
	./scripts/check-sources.sh
```

- [ ] **Step 5: Validate scripts**

Run:

```bash
chmod +x scripts/check-env.sh scripts/check-sources.sh scripts/fetch-fatfs.sh
bash -n scripts/check-env.sh scripts/check-sources.sh scripts/fetch-fatfs.sh
make check-sources
```

Expected: Bash syntax passes; source verification passes only when all gitlinks and checksums match.

### Task 6: M0 Acceptance and First Commit

**Files:**
- Verify: all M0 files

- [ ] **Step 1: Scan forbidden history and generated content**

Run:

```bash
test "$(git rev-list --all --count)" -eq 0
find . -path ./.git -prune -o -type f \( -name '*.o' -o -name '*.elf' -o -name '*.img' \) -print -quit | grep -q . && exit 1 || true
```

Expected before the first commit: no existing commits and no generated binaries.

- [ ] **Step 2: Run all M0 checks**

Run:

```bash
make check-env
make deps
make check-sources
git diff --check
git status --short
```

Expected: checks pass and only intentional M0 files and gitlinks are untracked or staged.

- [ ] **Step 3: Create the first commit after email verification**

Run:

```bash
git add .gitattributes .gitignore .gitmodules LICENSE Makefile README.md THIRD_PARTY.md docs scripts third_party
git commit -m "chore: establish reproducible project foundation"
git log -1 --format='%H%n%an <%ae>%n%s'
```

Expected: the author is `Quchaosheng` with the verified email, and no old repository history is present.
