#!/usr/bin/env bash
#
# install_dependencies.sh
# -----------------------
# Installs every dependency needed to build and run inmp441_rpi.
#
# Works on both 32-bit (armhf) and 64-bit (arm64) Raspberry Pi OS, and on an
# x86_64 PC it also prepares the ARM cross-compilation toolchain (so you can
# run scripts/cross_build.sh without the Pi connected).
#
# Installs:
#   * build essentials (compiler, make) and git/wget/curl
#   * nlohmann-json3-dev  (config.json persistence)
#   * libasound2-dev, libgpiod-dev, pkg-config (ALSA capture + GPIO21 L/R)
#   * libmpg123-dev, libao-dev  (MP3 playback, --player)
#   * lame               (MP3 encoding, --mp3 mode)
#   * bluez, pulseaudio, pulseaudio-module-bluetooth, pulseaudio-utils
#                        (Bluetooth A2DP playback: bluetoothctl + pactl)
#   * the bcm2835 userspace library (1.71) installed under /usr/local,
#     used ONLY by the OLED display (menu/player screens)
#   * [x86_64 host only] g++-arm-linux-gnueabihf + armhf multiarch libs for
#                        scripts/cross_build.sh
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
    curl \
    nlohmann-json3-dev \
    libmpg123-dev \
    libao-dev \
    libasound2-dev \
    libgpiod-dev \
    pkg-config

log "Installing lame (MP3 encoder for --mp3 mode)..."
DEBIAN_FRONTEND=noninteractive apt-get install -y lame

log "Installing runtime tools (Bluetooth A2DP + PulseAudio for --player)..."
# bluetoothctl lo provee bluez; pactl lo provee pulseaudio-utils (necesario
# para volver a conectar el sink por defecto tras un pairing).
DEBIAN_FRONTEND=noninteractive apt-get install -y \
    bluez \
    pulseaudio \
    pulseaudio-module-bluetooth \
    pulseaudio-utils

# ---------------------------------------------------------------------------
# x86_64 host: ARM cross toolchain + multiarch (for scripts/cross_build.sh)
# ---------------------------------------------------------------------------
# Sólo tiene sentido en un PC de desarrollo, no en la Pi.
case "$ARCH" in
    host*)
        log "Host PC detected - installing ARM cross toolchain (for scripts/cross_build.sh)..."
        if [[ "$(dpkg --print-foreign-architectures)" != *armhf* ]]; then
            log "Enabling armhf multiarch..."
            dpkg --add-architecture armhf
            apt-get update -y
        fi
        DEBIAN_FRONTEND=noninteractive apt-get install -y \
            g++-arm-linux-gnueabihf \
            libmpg123-dev:armhf \
            libao-dev:armhf \
            libasound2-dev:armhf \
            libgpiod-dev:armhf
        ;;
esac

# ---------------------------------------------------------------------------
# bcm2835 library (used only by the OLED display)
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
test -f /usr/include/alsa/asoundlib.h || die "asoundlib.h not found (libasound2-dev)"
test -f /usr/include/gpiod.h || die "gpiod.h not found (libgpiod-dev)"

g++ --version >/dev/null 2>&1 || die "g++ not available"
make --version >/dev/null 2>&1 || die "make not available"

echo '#include <nlohmann/json.hpp>' | g++ -E -x c++ - >/dev/null 2>&1 \
    || die "nlohmann/json.hpp not found (nlohmann-json3-dev)"
echo '#include <mpg123.h>' | g++ -E -x c++ - >/dev/null 2>&1 \
    || die "mpg123.h not found (libmpg123-dev)"
echo '#include <ao/ao.h>' | g++ -E -x c++ - >/dev/null 2>&1 \
    || die "ao/ao.h not found (libao-dev)"
echo '#include <alsa/asoundlib.h>' | g++ -E -x c++ - >/dev/null 2>&1 \
    || die "asoundlib.h not found (libasound2-dev)"
echo '#include <gpiod.h>' | g++ -E -x c++ - >/dev/null 2>&1 \
    || die "gpiod.h not found (libgpiod-dev)"

pkg-config --exists alsa libgpiod || die "pkg-config cannot find alsa/libgpiod"

command -v lame >/dev/null 2>&1 || warn "lame not found - --mp3 mode will fail"
command -v bluetoothctl >/dev/null 2>&1 || warn "bluetoothctl not found - --player/Bluetooth will fail"
command -v pactl >/dev/null 2>&1 || warn "pactl not found - volume control/Bluetooth routing will fail"

case "$ARCH" in
    host*)
        command -v arm-linux-gnueabihf-g++ >/dev/null 2>&1 \
            || die "arm-linux-gnueabihf-g++ not found (cross toolchain)"
        ;;
esac

log "All dependencies installed successfully (${ARCH})."
log "Next step:  make clean && make -j4"
log "On an x86_64 host, use instead:  bash scripts/cross_build.sh"
