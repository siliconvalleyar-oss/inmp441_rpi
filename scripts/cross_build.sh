#!/usr/bin/env bash
#
# cross_build.sh - cross-compile inmp441_rpi for the Raspberry Pi (ARM)
# from a normal x86_64 Linux PC, without needing the Pi connected.
#
# Produces bin/inmp441_rpi as a 32-bit ARM (armhf) executable - the same
# format the project builds on Raspberry Pi OS with 32-bit userspace.
#
# One-time requirements. Either install the ARM audio libs with apt
# (needs root):
#   sudo dpkg --add-architecture armhf && sudo apt-get update
#   sudo apt-get install g++-10-arm-linux-gnueabihf libmpg123-dev:armhf libao-dev:armhf
# or copy them from a Pi into the sysroot (no root needed), see the error
# messages below for the exact commands.
#
# IMPORTANT: the cross compiler MUST be GCC 10 (g++-10-arm-linux-gnueabihf).
# A GCC 13 cross build is ABI-incompatible with bullseye's libstdc++ 6.0.28
# and produces binaries that crash (the recorder menu segfaults on any input).
# The script refuses to build with GCC 13; see the "Compiler" section for how
# to supply a GCC 10 toolchain without root (extracted .debs).
#
# The alsa/libgpiod ARM headers+libraries are taken from the sysroot (apt
# multiarch or copied from a Pi, see section 1); bcm2835 is still cross-built
# into the sysroot for the OLED display (section 3). The I2S audio capture
# itself comes from the kernel driver (ALSA overlay), so there is no userspace
# peripheral library driving the microphone anymore.
#
# Linking notes (IMPORTANT):
#   The PC toolchain (e.g. Ubuntu 24.04 / Mint 22) ships glibc 2.39 headers
#   and crt objects, so a plain cross-link produces a binary that requires
#   GLIBC_2.38 - too new for the Pi 32 (bullseye, glibc 2.31). This script
#   therefore links against a minimal sysroot containing the Pi's *actual*
#   runtime libraries (libc 2.31, libstdc++ from GCC 10, crt1.o...) so the
#   binary only requires GLIBC_2.4 and runs on bullseye.
#
#   Populate that runtime part once from any 32-bit Pi (needs to happen only
#   if the check below complains):
#     ssh pi@<pi-ip> 'cd / && tar czf /tmp/arm_runtime.tgz \
#         lib/arm-linux-gnueabihf/ld-linux-armhf.so.3 \
#         lib/arm-linux-gnueabihf/libc.so.6 lib/arm-linux-gnueabihf/libc-*.so \
#         lib/arm-linux-gnueabihf/libm.so.6 lib/arm-linux-gnueabihf/libm-*.so \
#         lib/arm-linux-gnueabihf/libpthread.so.0 lib/arm-linux-gnueabihf/libpthread-*.so \
#         lib/arm-linux-gnueabihf/libdl.so.2 lib/arm-linux-gnueabihf/libdl-*.so \
#         lib/arm-linux-gnueabihf/librt.so.1 lib/arm-linux-gnueabihf/librt-*.so \
#         lib/arm-linux-gnueabihf/libgcc_s.so.1 \
#         usr/lib/arm-linux-gnueabihf/libstdc++.so.6 usr/lib/arm-linux-gnueabihf/libstdc++.so.6.0.* \
#         usr/lib/arm-linux-gnueabihf/libc.so usr/lib/arm-linux-gnueabihf/libm.so \
#         usr/lib/arm-linux-gnueabihf/libpthread.so usr/lib/arm-linux-gnueabihf/libdl.so \
#         usr/lib/arm-linux-gnueabihf/librt.so usr/lib/arm-linux-gnueabihf/libc_nonshared.a \
#         usr/lib/arm-linux-gnueabihf/crt1.o usr/lib/arm-linux-gnueabihf/Scrt1.o \
#         usr/lib/arm-linux-gnueabihf/crti.o usr/lib/arm-linux-gnueabihf/crtn.o \
#         && scp /tmp/arm_runtime.tgz pi@<pi-ip>:/tmp/'  # (scp pi@<pi-ip>:/tmp/arm_runtime.tgz .)
#   scp pi@<pi-ip>:/tmp/arm_runtime.tgz /tmp/
#   tar xzf /tmp/arm_runtime.tgz -C "${ARM_SYSROOT}"
#   (the script fixes up the dev symlinks automatically, so no manual ln needed)
#
#   Also populate the glibc 2.31 headers (same idea):
#     ssh pi@<pi-ip> 'cd / && tar czf /tmp/arm_headers.tgz --exclude=usr/include/c++ \
#         --exclude=usr/include/nlohmann usr/include'
#     scp pi@<pi-ip>:/tmp/arm_headers.tgz /tmp/ && tar xzf /tmp/arm_headers.tgz -C "${ARM_SYSROOT}"
#
# Usage:
#   bash scripts/cross_build.sh   (sysroot auto-detected, see above)
#   ARM_SYSROOT=/path/to/sysroot bash scripts/cross_build.sh  (explicit override)
#
# The resulting binary runs on the Pi, e.g.:
#   scp bin/inmp441_rpi pi@<pi-ip>:/tmp/ && ssh pi@<pi-ip> 'sudo /tmp/inmp441_rpi --version'

set -euo pipefail

GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m'

log()  { echo -e "${GREEN}[cross]${NC} $*"; }
warn() { echo -e "${YELLOW}[warn]${NC} $*"; }
die()  { echo -e "${RED}[error]${NC} $*" >&2; exit 1; }

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${ROOT_DIR}"

# ---- 0. Compiler: MUST be GCC 10 -------------------------------------------
# The Pi (bullseye) ships libstdc++ 6.0.28 (GCC 10). The app has to be
# compiled with GCC 10: a GCC 13 build is ABI-incompatible with that runtime
# (the GCC 13 headers emit newer inline libstdc++ code that crashes against
# the older shared library - e.g. the recorder menu's std::getline segfaults
# on every input). GCC 10 keeps headers and runtime consistent.
#
# Lookup order:
#   1. arm-linux-gnueabihf-g++-10 on PATH (apt-installed)
#   2. CROSS_GCC10_DIR (explicit override)
#   3. Common locations for an extracted package (no root needed):
#      ${HOME}/gcc10-cross, /mnt/disk/gcc10-cross, /opt/gcc10-cross
#
# Extracted layout (no-root alternative to apt):
#   mkdir -p ~/gcc10-cross && cd ~/gcc10-cross
#   apt-get download g++-10-arm-linux-gnueabihf gcc-10-arm-linux-gnueabihf \
#       cpp-10-arm-linux-gnueabihf libstdc++-10-dev-armhf-cross \
#       libgcc-10-dev-armhf-cross
#   for d in *.deb; do dpkg-deb -x "$d" .; done
CROSS_CXX="$(command -v arm-linux-gnueabihf-g++-10 || true)"
GCC10_DIR=""
if [[ -z "${CROSS_CXX}" ]]; then
    for candidate in "${CROSS_GCC10_DIR:-}" "${HOME}/gcc10-cross" \
                     "/mnt/disk/gcc10-cross" "/opt/gcc10-cross"; do
        if [[ -x "${candidate}/usr/bin/arm-linux-gnueabihf-g++-10" ]]; then
            CROSS_CXX="${candidate}/usr/bin/arm-linux-gnueabihf-g++-10"
            GCC10_DIR="${candidate}"
            break
        fi
    done
fi
if [[ -z "${CROSS_CXX}" ]]; then
    die "GCC 10 cross toolchain missing. GCC 13 cross builds are\n" \
        "ABI-incompatible with bullseye's libstdc++ 6.0.28 and produce\n" \
        "binaries that crash (std::getline segfault). Install GCC 10:\n" \
        "  sudo apt-get install g++-10-arm-linux-gnueabihf\n" \
        "or extract the .debs into ~/gcc10-cross (see header comment)."
fi

# An extracted (non-installed) toolchain needs the ARM binutils on the PATH:
# the GCC 10 driver invokes plain 'as'/'ld', which would otherwise resolve to
# the host's x86_64 tools. ~/gcc10-cross/bin gets symlinks to the ARM ones.
if [[ -n "${GCC10_DIR}" ]]; then
    mkdir -p "${GCC10_DIR}/bin"
    for t in as ld ar nm objdump objcopy ranlib strip readelf; do
        [[ -e "${GCC10_DIR}/bin/${t}" ]] || \
            ln -sf "/usr/bin/arm-linux-gnueabihf-${t}" "${GCC10_DIR}/bin/${t}"
    done
    export PATH="${GCC10_DIR}/bin:${PATH}"
fi

log "Using compiler: ${CROSS_CXX}"
CROSS_CC="$(dirname "${CROSS_CXX}")/arm-linux-gnueabihf-gcc-10"
[[ -x "${CROSS_CC}" ]] || CROSS_CC="$(command -v arm-linux-gnueabihf-gcc-10)"
CROSS_NM="arm-linux-gnueabihf-nm"
CROSS_OBJDUMP="arm-linux-gnueabihf-objdump"

# Sysroot selection: an explicit ARM_SYSROOT always wins; otherwise the first
# *populated* candidate below is used, so the script just works on machines
# that keep the sysroot somewhere else (e.g. /mnt/disk/arm-sysroot). If none
# is populated, fall back to ${HOME}/arm-sysroot and let the "populate from
# the Pi" error below guide the user.
if [[ -n "${ARM_SYSROOT:-}" ]]; then
    :  # explicit override, used as-is
else
    ARM_SYSROOT=""
    for candidate in "${HOME}/arm-sysroot" "/mnt/disk/arm-sysroot"; do
        if [[ -d "${candidate}" && -f "${candidate}/usr/lib/arm-linux-gnueabihf/crt1.o" ]]; then
            ARM_SYSROOT="${candidate}"
            break
        fi
    done
    ARM_SYSROOT="${ARM_SYSROOT:-${HOME}/arm-sysroot}"
fi
log "Using sysroot: ${ARM_SYSROOT}"

# ---- 1. ARM audio headers (multiarch) --------------------------------------
if ! echo '#include <mpg123.h>' | "${CROSS_CXX}" -isystem "${ARM_SYSROOT}/usr/include/arm-linux-gnueabihf" -isystem "${ARM_SYSROOT}/usr/include" -E -x c++ - >/dev/null 2>&1 ||
   ! echo '#include <ao/ao.h>'  | "${CROSS_CXX}" -isystem "${ARM_SYSROOT}/usr/include/arm-linux-gnueabihf" -isystem "${ARM_SYSROOT}/usr/include" -E -x c++ - >/dev/null 2>&1 ||
   ! echo '#include <alsa/asoundlib.h>' | "${CROSS_CXX}" -isystem "${ARM_SYSROOT}/usr/include/arm-linux-gnueabihf" -isystem "${ARM_SYSROOT}/usr/include" -E -x c++ - >/dev/null 2>&1 ||
   ! echo '#include <gpiod.h>' | "${CROSS_CXX}" -isystem "${ARM_SYSROOT}/usr/include/arm-linux-gnueabihf" -isystem "${ARM_SYSROOT}/usr/include" -E -x c++ - >/dev/null 2>&1; then
    die "ARM versions of mpg123/ao/alsa/gpiod are missing. Fix with one of:\n" \
        "  * apt (multiarch, needs root):\n" \
        "      sudo dpkg --add-architecture armhf && sudo apt-get update\n" \
        "      sudo apt-get install libmpg123-dev:armhf libao-dev:armhf \\\n" \
        "          libasound2-dev:armhf libgpiod-dev:armhf\n" \
        "  * or copy them from a Raspberry Pi into ${ARM_SYSROOT}:\n" \
        "      scp pi@<pi-ip>:/usr/include/mpg123.h pi@<pi-ip>:/usr/include/fmt123.h ${ARM_SYSROOT}/include/\n" \
        "      scp -r pi@<pi-ip>:/usr/include/ao ${ARM_SYSROOT}/include/\n" \
        "      scp -r pi@<pi-ip>:/usr/include/alsa ${ARM_SYSROOT}/include/\n" \
        "      scp pi@<pi-ip>:/usr/include/gpiod.h ${ARM_SYSROOT}/include/\n" \
        "      scp pi@<pi-ip>:/usr/lib/arm-linux-gnueabihf/libmpg123.so* ${ARM_SYSROOT}/lib/\n" \
        "      scp pi@<pi-ip>:/usr/lib/arm-linux-gnueabihf/libao.so* ${ARM_SYSROOT}/lib/\n" \
        "      scp pi@<pi-ip>:/usr/lib/arm-linux-gnueabihf/libasound.so* ${ARM_SYSROOT}/lib/\n" \
        "      scp pi@<pi-ip>:/usr/lib/arm-linux-gnueabihf/libgpiod.so* ${ARM_SYSROOT}/lib/"
fi

# ---- 2. Pi runtime libraries + headers (glibc 2.31 / libstdc++ GCC 10) ------
# (the header section numbering is kept from the GCC 13-era script)
# The exact libstdc++ version depends on the Pi the sysroot was populated from
# (bullseye ships 6.0.28, bookworm 6.0.30...). Derive it with a glob instead
# of hardcoding it.
LIBSTDCXX_SO="$(ls "${ARM_SYSROOT}"/usr/lib/arm-linux-gnueabihf/libstdc++.so.6.0.* 2>/dev/null | head -1 || true)"
if [[ ! -f "${ARM_SYSROOT}/usr/lib/arm-linux-gnueabihf/crt1.o" ||
      ! -f "${ARM_SYSROOT}/lib/arm-linux-gnueabihf/libc.so.6" ||
      -z "${LIBSTDCXX_SO}" ||
      ! -f "${ARM_SYSROOT}/usr/include/features.h" ]]; then
    die "Pi runtime libraries missing in ${ARM_SYSROOT}. Populate them once from a 32-bit Pi\n" \
        "  (ssh pi@<pi-ip> 'cd / && tar czf /tmp/arm_runtime.tgz lib/arm-linux-gnueabihf/ld-linux-armhf.so.3 lib/arm-linux-gnueabihf/libc.so.6 lib/arm-linux-gnueabihf/libc-*.so lib/arm-linux-gnueabihf/libm.so.6 lib/arm-linux-gnueabihf/libm-*.so lib/arm-linux-gnueabihf/libpthread.so.0 lib/arm-linux-gnueabihf/libpthread-*.so lib/arm-linux-gnueabihf/libdl.so.2 lib/arm-linux-gnueabihf/libdl-*.so lib/arm-linux-gnueabihf/librt.so.1 lib/arm-linux-gnueabihf/librt-*.so lib/arm-linux-gnueabihf/libgcc_s.so.1 usr/lib/arm-linux-gnueabihf/libstdc++.so.6 usr/lib/arm-linux-gnueabihf/libstdc++.so.6.0.* usr/lib/arm-linux-gnueabihf/libc.so usr/lib/arm-linux-gnueabihf/libm.so usr/lib/arm-linux-gnueabihf/libpthread.so usr/lib/arm-linux-gnueabihf/libdl.so usr/lib/arm-linux-gnueabihf/librt.so usr/lib/arm-linux-gnueabihf/libc_nonshared.a usr/lib/arm-linux-gnueabihf/crt1.o usr/lib/arm-linux-gnueabihf/Scrt1.o usr/lib/arm-linux-gnueabihf/crti.o usr/lib/arm-linux-gnueabihf/crtn.o && scp /tmp/arm_runtime.tgz <pc>:/tmp/'), then:\n" \
        "  scp pi@<pi-ip>:/tmp/arm_runtime.tgz /tmp/ && tar xzf /tmp/arm_runtime.tgz -C ${ARM_SYSROOT}\n" \
        "  And the glibc 2.31 headers:\n" \
        "  ssh pi@<pi-ip> 'cd / && tar czf /tmp/arm_headers.tgz --exclude=usr/include/c++ --exclude=usr/include/nlohmann usr/include'\n" \
        "  scp pi@<pi-ip>:/tmp/arm_headers.tgz /tmp/ && tar xzf /tmp/arm_headers.tgz -C ${ARM_SYSROOT}\n" \
        "  (the script then fixes up the dev symlinks automatically)"
fi

# The Pi's tar ships /usr/lib/... dev symlinks pointing at absolute paths
# (/lib/arm-linux-gnueabihf/...). Rewrite them relative to the sysroot and add
# the unversioned link names the linker needs (-lpthread, -ldl, -lrt, -lm,
# -lstdc++, -lgcc_s, and the top-level /lib/ld-linux-armhf.so.3).
RTLIB="${ARM_SYSROOT}/lib/arm-linux-gnueabihf"
DEVRLIB="${ARM_SYSROOT}/usr/lib/arm-linux-gnueabihf"
mkdir -p "${RTLIB}" "${DEVRLIB}"
ln -sfn "../../lib/arm-linux-gnueabihf/libc.so.6"      "${DEVRLIB}/libc.so"
ln -sfn "../../lib/arm-linux-gnueabihf/libm.so.6"      "${DEVRLIB}/libm.so"
ln -sfn "../../lib/arm-linux-gnueabihf/libpthread.so.0" "${DEVRLIB}/libpthread.so"
ln -sfn "../../lib/arm-linux-gnueabihf/libdl.so.2"     "${DEVRLIB}/libdl.so"
ln -sfn "../../lib/arm-linux-gnueabihf/librt.so.1"     "${DEVRLIB}/librt.so"
ln -sfn "../../lib/arm-linux-gnueabihf/libgcc_s.so.1"  "${DEVRLIB}/libgcc_s.so"
ln -sfn "$(basename "${LIBSTDCXX_SO}")"                "${DEVRLIB}/libstdc++.so"
ln -sfn "arm-linux-gnueabihf/ld-linux-armhf.so.3"      "${ARM_SYSROOT}/lib/ld-linux-armhf.so.3"

# ---- 3. bcm2835 into the local sysroot (used only by the OLED display) ------
# The I2S audio itself comes from the kernel driver (ALSA), so bcm2835 is no
# longer the capture backend - but the SSD1306 OLED still compiles against
# bcm2835.h and links libbcm2835.a, so it is cross-built into the sysroot.
BCM2835_VERSION="1.71"
BCM2835_URL="http://www.airspayce.com/mikem/bcm2835/bcm2835-${BCM2835_VERSION}.tar.gz"

# bcm2835 must be built against the sysroot's glibc 2.31 headers too; if the
# existing .a contains 64-bit/C23 redirect symbols it was built with the host
# headers and needs rebuilding.
if [[ -f "${ARM_SYSROOT}/include/bcm2835.h" && -f "${ARM_SYSROOT}/lib/libbcm2835.a" ]] &&
   ! "${CROSS_NM}" "${ARM_SYSROOT}/lib/libbcm2835.a" 2>/dev/null | grep -qE '__time64|__nanosleep64|__isoc23|__stat64_time64'; then
    log "bcm2835 already in ${ARM_SYSROOT} (skipping build)."
else
    if [[ -f "${ARM_SYSROOT}/lib/libbcm2835.a" ]]; then
        warn "libbcm2835.a was built with host headers; rebuilding with sysroot headers."
        rm -f "${ARM_SYSROOT}/lib/libbcm2835.a"
    fi
    log "Cross-building bcm2835-${BCM2835_VERSION} into ${ARM_SYSROOT}..."
    TMP="$(mktemp -d)"
    trap 'rm -rf "${TMP}"' EXIT
    wget -q -O "${TMP}/bcm2835.tar.gz" "${BCM2835_URL}" || die "cannot download ${BCM2835_URL}"
    tar -xzf "${TMP}/bcm2835.tar.gz" -C "${TMP}"
    cd "${TMP}/bcm2835-${BCM2835_VERSION}"
    CC="${CROSS_CC}" ./configure --host=arm-linux-gnueabihf >/dev/null
    make CC="${CROSS_CC}" CFLAGS="-isystem ${ARM_SYSROOT}/usr/include/arm-linux-gnueabihf -isystem ${ARM_SYSROOT}/usr/include" >/dev/null
    mkdir -p "${ARM_SYSROOT}/include" "${ARM_SYSROOT}/lib"
    cp src/bcm2835.h "${ARM_SYSROOT}/include/"
    cp src/libbcm2835.a "${ARM_SYSROOT}/lib/"
    cd "${ROOT_DIR}"
fi

# ---- 4. alsa/libgpiod runtime libraries in the sysroot ----------------------
# No build step is needed for these: they come from apt multiarch or are
# copied from a Pi (see section 1). Just make sure the linker can find them.
if ! ls "${ARM_SYSROOT}"/lib/arm-linux-gnueabihf/libasound.so.* >/dev/null 2>&1; then
    die "libasound (ALSA) missing in ${ARM_SYSROOT}. Copy it from a Pi:\n" \
        "  scp pi@<pi-ip>:/usr/lib/arm-linux-gnueabihf/libasound.so* ${ARM_SYSROOT}/lib/"
fi
if ! ls "${ARM_SYSROOT}"/lib/arm-linux-gnueabihf/libgpiod.so.* >/dev/null 2>&1; then
    die "libgpiod missing in ${ARM_SYSROOT}. Copy it from a Pi:\n" \
        "  scp pi@<pi-ip>:/usr/lib/arm-linux-gnueabihf/libgpiod.so* ${ARM_SYSROOT}/lib/"
fi

# ---- 5. Link flags against the Pi runtime sysroot ---------------------------
# (the header section numbering is kept from the GCC 13-era script)
CRT1="${ARM_SYSROOT}/usr/lib/arm-linux-gnueabihf/crt1.o"
CRTI="${ARM_SYSROOT}/usr/lib/arm-linux-gnueabihf/crti.o"
CRTN="${ARM_SYSROOT}/usr/lib/arm-linux-gnueabihf/crtn.o"
CRTBEGIN="$("${CROSS_CXX}" -print-file-name=crtbegin.o)"
CRTEND="$("${CROSS_CXX}" -print-file-name=crtend.o)"

# -nostdlib/-nostartfiles: the toolchain's own -L dirs are searched BEFORE
# user -L flags, so the driver's implicit -lc would resolve to the HOST libc
# (glibc 2.39) and pull in GLIBC_PRIVATE mismatches with the Pi's libpthread.
# Instead every runtime library is named explicitly from the sysroot using
# exact file names (-l:), so nothing host-side leaks into the binary.
#
# -rpath-link lets the linker inspect the DT_NEEDED chain (libstdc++ ->
# libm/libgcc_s) at link time. -dynamic-linker sets the ELF interpreter to the
# Pi's loader path (bullseye ships /lib/ld-linux-armhf.so.3). libc is listed
# so its GLIBC_PRIVATE symbols satisfy libpthread/libdl, and libc_nonshared.a
# provides __libc_csu_* (normally pulled by the libc.so linker script, which
# we bypass). -l:ld-linux-armhf.so.3 additionally records the loader as a
# DT_NEEDED, matching what glibc-linked binaries normally carry (empirically
# needed for the GLIBC_PRIVATE chain to resolve - keep it).
SYSROOT_LDFLAGS="-nostdlib -no-pie -nostartfiles \
-Wl,--no-as-needed \
-Wl,-dynamic-linker=/lib/ld-linux-armhf.so.3 \
-Wl,-rpath-link=${ARM_SYSROOT}/lib/arm-linux-gnueabihf \
-Wl,-rpath-link=${ARM_SYSROOT}/usr/lib/arm-linux-gnueabihf \
-L${ARM_SYSROOT}/lib \
-L${ARM_SYSROOT}/lib/arm-linux-gnueabihf \
-L${ARM_SYSROOT}/usr/lib/arm-linux-gnueabihf \
-lbcm2835 -lasound -lgpiod -lmpg123 -lao \
-l:libpthread.so.0 -l:libdl.so.2 -l:librt.so.1 -l:libm.so.6 \
-l:libstdc++.so.6 -l:libgcc_s.so.1 \
-l:libc.so.6 -l:ld-linux-armhf.so.3 \
${ARM_SYSROOT}/usr/lib/arm-linux-gnueabihf/libc_nonshared.a"

# ---- 5. Build ---------------------------------------------------------------
log "Cleaning previous build (may contain host/Pi objects)..."
make clean >/dev/null 2>&1 || true
mkdir -p obj

# Use the Pi's glibc 2.31 headers (NOT the host's glibc 2.39 headers, which
# redirect time()/stat()/strtol() to __time64/__isoc23_* symbols that do not
# exist in bullseye).
#
# libstdc++ 13's <cstdlib> etc. use #include_next for the C standard headers,
# which skips the directory containing the including header - so a plain
# -isystem pointing at the sysroot would be bypassed and the host's glibc 2.39
# headers would still win. Instead we rebuild GCC's own include search order
# with -nostdinc: keep the toolchain's C/C++ standard-library dirs (libstdc++
# 13, gcc fixed includes) but DROP the host's glibc include dirs (which resolve
# to /usr/arm-linux-gnueabihf/include, /usr/include, /usr/local/include),
# replacing them with the sysroot's glibc 2.31 headers. Host paths are matched
# after canonicalizing (the -v output contains unresolved ../.. components).
SYSROOT_CPPFLAGS="-nostdinc"
while IFS= read -r d; do
    [ -z "${d}" ] && continue
    case "$(realpath -m "${d}")" in
        /usr/arm-linux-gnueabihf/include|/usr/include|/usr/local/include)
            continue ;;          # host glibc 2.39 - skip
    esac
    SYSROOT_CPPFLAGS+=" -isystem ${d%/}"
done < <("${CROSS_CXX}" -v -E -x c++ /dev/null 2>&1 \
    | sed -n '/search starts here/,/End of search list/p' \
    | grep -E '^ /' | sed 's|^ ||')
SYSROOT_CPPFLAGS+=" -isystem ${ARM_SYSROOT}/usr/include/arm-linux-gnueabihf -isystem ${ARM_SYSROOT}/usr/include"

# GCC 10's libstdc++ already matches the Pi's runtime, so no compat shim is
# needed (the GCC 13-era scripts/cross/compat_shim.cpp stays for reference).
# PKG_CFLAGS/PKG_LDLIBS are forced empty so the host's pkg-config alsa/gpiod
# flags do not leak host include/link paths into the cross build; the sysroot
# paths come from SYSROOT_CPPFLAGS / SYSROOT_LDFLAGS instead.
log "Cross-compiling with ${CROSS_CXX} (sysroot: ${ARM_SYSROOT})..."
make -j"$(nproc)" \
    CXX="${CROSS_CXX}" \
    CXXFLAGS_EXTRA="${SYSROOT_CPPFLAGS}" \
    BCM2835_INCLUDE="${ARM_SYSROOT}/include" \
    PKG_CFLAGS= \
    PKG_LDLIBS= \
    LDFLAGS="${SYSROOT_LDFLAGS}" \
    CRT_BEGIN="${CRT1} ${CRTI} ${CRTBEGIN}" \
    CRT_END="${CRTEND} ${CRTN}"

# ---- 6. Verify --------------------------------------------------------------
log "Verifying (GLIBC/GLIBCXX must be <= bullseye: GLIBC_2.31 / GLIBCXX_3.4.28)..."
file bin/inmp441_rpi

GLIBC_NEEDED="$("${CROSS_OBJDUMP}" -T bin/inmp441_rpi 2>/dev/null | grep -oE 'GLIBC_[0-9.]+' | sort -Vu | tail -1 || true)"
GLIBCXX_NEEDED="$("${CROSS_OBJDUMP}" -T bin/inmp441_rpi 2>/dev/null | grep -oE 'GLIBCXX_[0-9.]+' | sort -Vu | tail -1 || true)"
log "max GLIBC  version required: ${GLIBC_NEEDED:-unknown}"
log "max GLIBCXX version required: ${GLIBCXX_NEEDED:-unknown}"

if ! file bin/inmp441_rpi | grep -q 'ELF 32-bit'; then
    die "bin/inmp441_rpi is not a 32-bit ARM executable - link failed."
fi
if [[ -n "${GLIBC_NEEDED}" ]] &&
   [[ "$(printf '%s\n%s\n' "${GLIBC_NEEDED}" 'GLIBC_2.31' | sort -V | tail -1)" != 'GLIBC_2.31' ]]; then
    die "binary requires ${GLIBC_NEEDED}, but bullseye has GLIBC_2.31 - sysroot mismatch."
fi
if [[ -n "${GLIBCXX_NEEDED}" ]] &&
   [[ "$(printf '%s\n%s\n' "${GLIBCXX_NEEDED}" 'GLIBCXX_3.4.28' | sort -V | tail -1)" != 'GLIBCXX_3.4.28' ]]; then
    die "binary requires ${GLIBCXX_NEEDED}, but bullseye ships libstdc++ with GLIBCXX_3.4.28 - sysroot mismatch."
fi

log "Done. Copy to the Pi and run, e.g.:"
log "  scp bin/inmp441_rpi pi@<pi-ip>:/tmp/ && ssh pi@<pi-ip> 'sudo /tmp/inmp441_rpi --version'"
