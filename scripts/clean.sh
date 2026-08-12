#!/usr/bin/env bash
#
# clean.sh - removes all build artifacts (obj/, bin/).

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${ROOT_DIR}"
make clean
echo "Done."
