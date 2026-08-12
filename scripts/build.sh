#!/usr/bin/env bash
#
# build.sh - convenience wrapper around make.
#
# Usage:
#   bash scripts/build.sh          # full build (make all)
#   bash scripts/build.sh clean    # remove obj/ and bin/
#   bash scripts/build.sh test     # run the unit tests (no hardware needed)

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${ROOT_DIR}"

case "${1:-all}" in
    clean)
        make clean
        ;;
    test)
        make test
        ;;
    all|"")
        make all
        ;;
    *)
        echo "Unknown target: $1" >&2
        echo "Usage: bash scripts/build.sh [all|clean|test]" >&2
        exit 1
        ;;
esac
