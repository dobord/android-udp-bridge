#!/usr/bin/env bash
set -euo pipefail

# Exports all commits on the current branch (oldest-first) into translations.txt
# Format per commit:
# ===COMMIT=== <sha>
# <commit message lines>
# ===END===

OUT_FILE="translations.txt"
echo "Exporting commits to $OUT_FILE"
rm -f "$OUT_FILE"

for sha in $(git rev-list --reverse HEAD); do
  echo "===COMMIT=== $sha" >> "$OUT_FILE"
  git log -n1 --pretty=format:%B "$sha" >> "$OUT_FILE"
  echo "\n===END===\n" >> "$OUT_FILE"
done

echo "Done. Edit $OUT_FILE: replace the commit message block for a commit with the translated English message (keep the markers)."
