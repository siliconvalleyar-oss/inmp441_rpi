#!/usr/bin/env bash
#
# run.sh - runs the binary. Recording needs no root (ALSA + libgpiod); use
# sudo only when you need the OLED display in the menu/player screens.
#
# Usage:
#   bash scripts/run.sh                # interactive menu (default)
#   bash scripts/run.sh --wav cap.wav -d 10
#   bash scripts/run.sh --level

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BINARY="${ROOT_DIR}/bin/inmp441_rpi"

if [[ ! -x "${BINARY}" ]]; then
    echo "Binary not found. Building first..." >&2
    make -C "${ROOT_DIR}" all
fi

exec "${BINARY}" "$@"
