#!/usr/bin/env bash
set -euo pipefail

# WARNING: rewrites history on the current branch using translations.txt
# Make a backup branch before running.

REPO_ROOT=$(git rev-parse --show-toplevel)
BRANCH=$(git rev-parse --abbrev-ref HEAD)

cat <<EOF
This will rewrite history on branch: $BRANCH
Make sure you've pushed or created a backup branch first.
EOF

read -p "Create a backup branch named backup/$BRANCH and continue? (yes/no) " ans
if [ "$ans" != "yes" ]; then
  echo "Aborted."; exit 1
fi

git branch -f "backup/$BRANCH" "$BRANCH"

# Ensure msg_replace.py is executable
chmod +x "$REPO_ROOT/scripts/msg_replace.py"

# Run filter-branch on the branch to replace commit messages
# Keep original branch as $BRANCH-old
ORIG="$BRANCH-old"
git branch -f "$ORIG" "$BRANCH"

# Run the msg filter using the script
git filter-branch -f --msg-filter "$REPO_ROOT/scripts/msg_replace.py" -- "$ORIG"

cat <<EOF
Done. The rewritten history refs are under refs/heads/ (the filtered branch is named '$ORIG').
If the results look good, replace the original branch:
  git branch -f $BRANCH $ORIG
Then force-push: git push --force origin $BRANCH
EOF
