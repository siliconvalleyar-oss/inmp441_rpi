#!/usr/bin/env bash
#
# install_commit_hook.sh - installs a commit-msg git hook that enforces
# Conventional Commits (see README.md -> "Commit conventions" and
# docs/LEARNINGS.md).
#
# Usage:  bash scripts/install_commit_hook.sh
# Idempotent: re-running it simply reinstalls the hook. An existing custom
# hook is backed up (commit-msg.bak) before being replaced.

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${ROOT_DIR}"

# Locate the real hooks directory (works with worktrees and core.hooksPath).
if ! HOOKS_DIR="$(git rev-parse --git-path hooks 2>/dev/null)" || [[ -z "${HOOKS_DIR}" ]]; then
    echo "error: not a git repository - run this from inside the repo" >&2
    exit 1
fi
mkdir -p "${HOOKS_DIR}"
HOOK="${HOOKS_DIR}/commit-msg"

# Back up an existing custom hook (but not our own previous install).
if [[ -e "${HOOK}" ]] && ! grep -q "enforce conventional commit messages" "${HOOK}" 2>/dev/null; then
    cp "${HOOK}" "${HOOK}.bak"
    echo "backed up existing hook to ${HOOK}.bak"
fi

cat > "${HOOK}" <<'HOOK'
#!/bin/sh
# commit-msg hook: enforce conventional commit messages.
# See README.md -> "Commit conventions" and docs/LEARNINGS.md.

msg_file="${1:-}"
if [ -z "${msg_file}" ]; then
    echo "error: commit-msg hook requires the message file argument" >&2
    exit 1
fi
first_line=$(sed -n '1p' "${msg_file}")

# Let git-generated messages through (merge, revert, fixup, squash, amend...).
case "${first_line}" in
    Merge\ *)  exit 0 ;;
    Revert\ *) exit 0 ;;
    fixup!*)   exit 0 ;;
    squash!*)  exit 0 ;;
    amend!*)   exit 0 ;;
esac

if printf '%s\n' "${first_line}" | \
   grep -qE '^(feat|fix|docs|chore|refactor|test|build|ci|perf|style)(\([a-zA-Z0-9._/-]+\))?!?: .+'; then
    exit 0
fi

echo "error: commit message must follow conventional commits" >&2
echo "  type(scope)!: description   e.g. 'feat(menu): add gain option'" >&2
echo "  types: feat fix docs chore refactor test build ci perf style" >&2
echo "got: ${first_line}" >&2
exit 1
HOOK

chmod +x "${HOOK}"
echo "installed conventional-commit hook at ${HOOK}"
