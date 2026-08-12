#!/usr/bin/env bash
#
# run.sh - runs the binary with sudo (bcm2835 needs /dev/mem access).
#
# Usage:
#   bash scripts/run.sh --info
#   bash scripts/run.sh --wav capture.wav -d 10
#   bash scripts/run.sh --level

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BINARY="${ROOT_DIR}/bin/inmp441_rpi"

if [[ ! -x "${BINARY}" ]]; then
    echo "Binary not found. Building first..." >&2
    make -C "${ROOT_DIR}" all
fi

exec sudo "${BINARY}" "$@"
