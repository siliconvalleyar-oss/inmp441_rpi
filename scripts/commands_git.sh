#!/usr/bin/env bash
#
# commands_git.sh - dump of the repository's git tag/version state.
#
# All queries are read-only and safe to run at any time. Prints, in order:
#   1. all local tags (full list + latest)
#   2. tags pushed to the remote (origin)
#   3. commit history decorated with tags and by date
#
# No `set -e` on purpose: every section is an independent read-only query and
# one failing command (e.g. git ls-remote without network) must not abort the
# rest of the dump.

# All local tags (oldest first).
echo "== all local tags =="
git --no-pager tag

# Latest tag.
echo
echo "== latest tag (git describe) =="
git describe --tags --abbrev=0

# Latest tag (alternative).
echo
echo "== latest tag (tail) =="
git tag | tail -1

# Tags pushed to the remote (compare against the local list to spot a local
# tag that was never pushed - the project rule is: no push without its tag).
echo
echo "== tags on origin (pushed) =="
git ls-remote --tags origin

# Commit history with tags and dates.
echo
echo "== history with tags/dates =="
git --no-pager log --tags --simplify-by-decoration --oneline \
    --format="%h %ad %s" --date=format:"%Y-%m-%d %H:%M:%S"

# History of origin/main by date.
echo
echo "== origin/main history by date =="
git --no-pager log origin/main --oneline \
    --format="%h %ad %s" --date=format:"%Y-%m-%d %H:%M:%S"
