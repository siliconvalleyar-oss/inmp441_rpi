#!/usr/bin/env bash
#
# check_prompt.sh - the "PROMPT.md loop": detect new feature requests.
#
# The workflow: PROMPT.md keeps the user's pending requests. Each time work
# finishes, run this script; if it reports new items, implement them, update
# TODO.md (the verification checklist), commit, and run it again until it says
# there is nothing new.
#
# Usage:
#   bash scripts/check_prompt.sh
#
# Exit codes: 0 = no new items, 1 = PROMPT.md has uncommitted changes.

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${ROOT_DIR}"

if [[ ! -f PROMPT.md ]]; then
    echo "PROMPT.md does not exist - nothing to check."
    exit 0
fi

if git diff --quiet -- PROMPT.md; then
    echo "PROMPT.md: no changes since the last commit - no new items."
    exit 0
fi

echo "PROMPT.md: NEW ITEMS DETECTED (uncommitted changes):"
echo "-----------------------------------------------------"
git diff -- PROMPT.md
echo "-----------------------------------------------------"
echo "Implement the new items, update TODO.md with the verification"
echo "checklist, commit, then re-run this script until it reports no"
echo "changes (that closes the loop)."
exit 1
