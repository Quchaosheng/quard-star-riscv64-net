#!/usr/bin/env bash
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)

pairs='README.md|README.zh-CN.md
THIRD_PARTY.md|THIRD_PARTY.zh-CN.md
docs/build-debug.md|docs/build-debug.zh-CN.md
docs/limitations.md|docs/limitations.zh-CN.md
docs/performance-baseline.md|docs/performance-baseline.zh-CN.md
docs/qemu-demo.md|docs/qemu-demo.zh-CN.md
docs/source-migration.md|docs/source-migration.zh-CN.md'

printf '%s\n' "$pairs" | while IFS='|' read -r english chinese; do
  test -s "$root/$english"
  test -s "$root/$chinese"
  chinese_name=${chinese##*/}
  english_name=${english##*/}
  grep -Fq "[$chinese_name]" "$root/$english" ||
    grep -Fq "($chinese_name)" "$root/$english"
  grep -Fq "($english_name)" "$root/$chinese"
done

"${PYTHON:-python3}" - "$root" <<'PY'
import pathlib
import re
import sys

root = pathlib.Path(sys.argv[1])
documents = [root / "README.md", root / "README.zh-CN.md"]
documents.extend(root.glob("docs/*.md"))
documents.extend((root / name for name in ("THIRD_PARTY.md", "THIRD_PARTY.zh-CN.md")))
pattern = re.compile(r"\]\(([^)]+)\)")

missing = []
for document in documents:
    text = document.read_text(encoding="utf-8")
    in_fence = False
    for line_number, line in enumerate(text.splitlines(), 1):
        if line.lstrip().startswith("```"):
            in_fence = not in_fence
            continue
        if in_fence:
            continue
        for match in pattern.finditer(line):
            target = match.group(1).split("#", 1)[0]
            if not target or target.startswith(("http://", "https://", "mailto:")):
                continue
            resolved = (document.parent / target).resolve()
            if not resolved.exists():
                missing.append(f"{document.relative_to(root)}:{line_number}: {target}")

if missing:
    raise SystemExit("missing local Markdown targets:\n" + "\n".join(missing))
PY

for marker in \
  'QS:TEST_PASS:m8-smoke' \
  'QS:TRUSTED_SCHED_OK' \
  'QS:PMP_UNTRUSTED_DENY_OK' \
  'QS:PMP_TRUSTED_DENY_OK'; do
  grep -Fq "$marker" "$root/README.md"
  grep -Fq "$marker" "$root/README.zh-CN.md"
done

echo 'PASS: bilingual documentation contracts'
