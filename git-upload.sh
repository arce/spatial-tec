#!/usr/bin/env bash
# Usage: ./git-upload.sh ["commit message"]
# Pulls, commits and pushes; stops at the first problem instead of
# committing a half-merged tree.
set -euo pipefail

msg="${1:-update}"
branch="$(git rev-parse --abbrev-ref HEAD)"

# 1. Bring in remote changes; a failed pull (e.g. merge conflict) aborts here.
git pull --no-edit origin "$branch" || {
  echo "ERROR: git pull failed. Resolve the conflicts, then run this script again." >&2
  exit 1
}

# 2. Never commit unresolved merge-conflict markers.
if git grep -nE '^(<<<<<<< |=======$|>>>>>>> )' -- . ':!git-upload.sh' >/dev/null 2>&1; then
  echo "ERROR: unresolved merge-conflict markers found:" >&2
  git grep -nE '^(<<<<<<< |=======$|>>>>>>> )' -- . ':!git-upload.sh' >&2 || true
  exit 1
fi

git add -A

if git diff --cached --quiet; then
  echo "Nothing to commit."
else
  git commit -m "$msg"
fi

git push origin "$branch"
