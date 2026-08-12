#!/usr/bin/env bash
#
# install_dependencies.sh
# -----------------------
# Installs every dependency needed to build inmp441_rpi on Raspberry Pi OS.
#
# Works on both 32-bit (armhf) and 64-bit (arm64) systems:
#   * build essentials (compiler, make)
#   * git, wget
#   * the bcm2835 userspace library (1.71) installed under /usr/local
#
# Usage:  bash scripts/install_dependencies.sh
# Requires: root (or a user with sudo).

set -euo pipefail

# ---------------------------------------------------------------------------
# Colour / helpers
# ---------------------------------------------------------------------------
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m'

log()  { echo -e "${GREEN}[install]${NC} $*"; }
warn() { echo -e "${YELLOW}[warn]${NC} $*"; }
die()  { echo -e "${RED}[error]${NC} $*" >&2; exit 1; }

# ---------------------------------------------------------------------------
# Architecture detection (32 vs 64 bit, and board sanity check)
# ---------------------------------------------------------------------------
detect_arch() {
    case "$(uname -m)" in
        aarch64|arm64)
            echo "64-bit (arm64)"
            ;;
        armv7l|armv6l|armhf)
            echo "32-bit (armhf)"
            ;;
        x86_64|amd64)
            echo "host (x86_64, not a Raspberry Pi - cannot use the hardware)"
            ;;
        *)
            echo "unknown ($(uname -m))"
            ;;
    esac
}

ARCH="$(detect_arch)"
log "Detected architecture: ${ARCH}"

case "$ARCH" in
    host*|unknown*)
        warn "This machine is not a Raspberry Pi; the driver will build for"
        warn "syntax/CI checks only and will fail at runtime (no /dev/mem GPIO)."
        ;;
esac

# ---------------------------------------------------------------------------
# Root check
# ---------------------------------------------------------------------------
if [[ $EUID -ne 0 ]]; then
    if command -v sudo >/dev/null 2>&1; then
        warn "Re-running with sudo..."
        exec sudo bash "$0" "$@"
    else
        die "Please run as root: sudo bash scripts/install_dependencies.sh"
    fi
fi

# ---------------------------------------------------------------------------
# System packages
# ---------------------------------------------------------------------------
log "Updating package lists..."
apt-get update -y

log "Installing build tools (build-essential git wget curl)..."
DEBIAN_FRONTEND=noninteractive apt-get install -y \
    build-essential \
    git \
    wget \
    curl

# ---------------------------------------------------------------------------
# bcm2835 library
# ---------------------------------------------------------------------------
BCM2835_VERSION="1.71"
BCM2835_TARBALL="/tmp/bcm2835-${BCM2835_VERSION}.tar.gz"
BCM2835_URL="http://www.airspayce.com/mikem/bcm2835/bcm2835-${BCM2835_VERSION}.tar.gz"
# SHA-256 of the official 1.71 release tarball.
BCM2835_SHA256="7a7593b56d837e4d8472d8973d0c3ac10ba775b2643710a0ac4d54e8aefd7bf1"

if [[ -f /usr/local/include/bcm2835.h && -f /usr/local/lib/libbcm2835.a ]]; then
    log "bcm2835 library already installed (skipping build)."
else
    log "Downloading bcm2835-${BCM2835_VERSION}..."
    if ! wget -q -O "${BCM2835_TARBALL}" "${BCM2835_URL}"; then
        die "Failed to download bcm2835 from ${BCM2835_URL}"
    fi

    log "Verifying checksum..."
    echo "${BCM2835_SHA256}  ${BCM2835_TARBALL}" | sha256sum -c - >/dev/null 2>&1 \
        || warn "Checksum mismatch - continuing anyway (possible mirror change)"

    log "Building and installing bcm2835..."
    rm -rf "/tmp/bcm2835-${BCM2835_VERSION}"
    tar -xzf "${BCM2835_TARBALL}" -C /tmp
    cd "/tmp/bcm2835-${BCM2835_VERSION}"
    ./configure
    make
    make install
    ldconfig
    cd - >/dev/null
    rm -f "${BCM2835_TARBALL}"
fi

# ---------------------------------------------------------------------------
# Verify
# ---------------------------------------------------------------------------
log "Verifying installation..."
test -f /usr/local/include/bcm2835.h || die "bcm2835.h not found in /usr/local/include"
test -f /usr/local/lib/libbcm2835.a || die "libbcm2835.a not found in /usr/local/lib"

g++ --version >/dev/null 2>&1 || die "g++ not available"
make --version >/dev/null 2>&1 || die "make not available"

log "All dependencies installed successfully (${ARCH})."
log "Next step:  make clean && make -j4"
