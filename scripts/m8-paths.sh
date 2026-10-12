#!/usr/bin/env bash
set -eu

# Maps changed repository paths to the M8 runtime tier for a pull request, so
# documentation-only changes stay fast. Reads a NUL- or newline-separated path
# list on stdin and prints "full" or "none".
#
# Only documentation paths can be proven irrelevant to the built image, so
# everything else takes the full M8 acceptance run: kernel/, trusted/,
# platform/, patches/, third_party/, user/, the runtime build and test scripts,
# Makefile and the workflow itself. There is deliberately no reduced runtime
# tier, because a partial smoke would be indistinguishable from M8 acceptance.
root=${QS_ROOT:-$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)}

doc_re='^([^/]*\.md$|docs/|LICENSE$)'

while IFS= read -r -d '' path || [ -n "$path" ]; do
  [ -n "$path" ] || continue
  case "$path" in
    */) continue ;;
  esac
  if ! printf '%s\n' "$path" | grep -Eq "$doc_re"; then
    echo full
    exit 0
  fi
done

echo none
