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
#   sudo apt-get install g++-arm-linux-gnueabihf libmpg123-dev:armhf libao-dev:armhf
# or copy them from a Pi into the sysroot (no root needed), see the error
# messages below for the exact commands.
#
# The bcm2835 library is cross-compiled automatically into a local sysroot
# (no root needed): ${HOME}/arm-sysroot by default, override with ARM_SYSROOT.
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
#   ln -sf arm-linux-gnueabihf/ld-linux-armhf.so.3 "${ARM_SYSROOT}"/lib/ld-linux-armhf.so.3
#   ln -sf libstdc++.so.6.0.28 "${ARM_SYSROOT}"/usr/lib/arm-linux-gnueabihf/libstdc++.so
#
#   Also populate the glibc 2.31 headers (same idea):
#     ssh pi@<pi-ip> 'cd / && tar czf /tmp/arm_headers.tgz --exclude=usr/include/c++ \
#         --exclude=usr/include/nlohmann usr/include'
#     scp pi@<pi-ip>:/tmp/arm_headers.tgz /tmp/ && tar xzf /tmp/arm_headers.tgz -C "${ARM_SYSROOT}"
#
# Usage:
#   bash scripts/cross_build.sh
#   ARM_SYSROOT=/mnt/disk/arm-sysroot bash scripts/cross_build.sh
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

CROSS_CXX="arm-linux-gnueabihf-g++"
CROSS_CC="arm-linux-gnueabihf-gcc"
CROSS_NM="arm-linux-gnueabihf-nm"
CROSS_OBJDUMP="arm-linux-gnueabihf-objdump"
ARM_SYSROOT="${ARM_SYSROOT:-${HOME}/arm-sysroot}"

# ---- 1. Cross toolchain -----------------------------------------------------
command -v "${CROSS_CXX}" >/dev/null 2>&1 || die \
    "cross toolchain missing. Install it with:\n" \
    "  sudo apt-get install g++-arm-linux-gnueabihf"

# ---- 2. ARM audio headers (multiarch) --------------------------------------
if ! echo '#include <mpg123.h>' | "${CROSS_CXX}" -isystem "${ARM_SYSROOT}/usr/include/arm-linux-gnueabihf" -isystem "${ARM_SYSROOT}/usr/include" -E -x c++ - >/dev/null 2>&1 ||
   ! echo '#include <ao/ao.h>'  | "${CROSS_CXX}" -isystem "${ARM_SYSROOT}/usr/include/arm-linux-gnueabihf" -isystem "${ARM_SYSROOT}/usr/include" -E -x c++ - >/dev/null 2>&1; then
    die "ARM versions of mpg123/ao are missing. Fix with one of:\n" \
        "  * apt (multiarch, needs root):\n" \
        "      sudo dpkg --add-architecture armhf && sudo apt-get update\n" \
        "      sudo apt-get install libmpg123-dev:armhf libao-dev:armhf\n" \
        "  * or copy them from a Raspberry Pi into ${ARM_SYSROOT}:\n" \
        "      scp pi@<pi-ip>:/usr/include/mpg123.h pi@<pi-ip>:/usr/include/fmt123.h ${ARM_SYSROOT}/include/\n" \
        "      scp -r pi@<pi-ip>:/usr/include/ao ${ARM_SYSROOT}/include/\n" \
        "      scp pi@<pi-ip>:/usr/lib/arm-linux-gnueabihf/libmpg123.so* ${ARM_SYSROOT}/lib/\n" \
        "      scp pi@<pi-ip>:/usr/lib/arm-linux-gnueabihf/libao.so* ${ARM_SYSROOT}/lib/"
fi

# ---- 3. Pi runtime libraries + headers (glibc 2.31 / libstdc++ GCC 10) ------
if [[ ! -f "${ARM_SYSROOT}/usr/lib/arm-linux-gnueabihf/crt1.o" ||
      ! -f "${ARM_SYSROOT}/lib/arm-linux-gnueabihf/libc.so.6" ||
      ! -f "${ARM_SYSROOT}/usr/lib/arm-linux-gnueabihf/libstdc++.so.6.0.28" ||
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
ln -sfn "libstdc++.so.6.0.28"                          "${DEVRLIB}/libstdc++.so"
ln -sfn "arm-linux-gnueabihf/ld-linux-armhf.so.3"      "${ARM_SYSROOT}/lib/ld-linux-armhf.so.3"

# ---- 4. bcm2835 into the local sysroot (no root needed) ---------------------
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

# ---- 5. Link flags against the Pi runtime sysroot ---------------------------
CRT1="${ARM_SYSROOT}/usr/lib/arm-linux-gnueabihf/crt1.o"
CRTI="${ARM_SYSROOT}/usr/lib/arm-linux-gnueabihf/crti.o"
CRTN="${ARM_SYSROOT}/usr/lib/arm-linux-gnueabihf/crtn.o"
CRTBEGIN="$("${CROSS_CXX}" -print-file-name=crtbegin.o)"
CRTEND="$("${CROSS_CXX}" -print-file-name=crtend.o)"

# --sysroot makes the libc.so linker script resolve its absolute paths
# (/lib/arm-linux-gnueabihf/...) inside our sysroot. -rpath-link lets the
# linker inspect the DT_NEEDED chain (libstdc++ -> libm/libgcc_s) at link time.
SYSROOT_LDFLAGS="-Wl,--sysroot=${ARM_SYSROOT} \
-Wl,--no-as-needed \
-Wl,-rpath-link=${ARM_SYSROOT}/lib/arm-linux-gnueabihf \
-Wl,-rpath-link=${ARM_SYSROOT}/usr/lib/arm-linux-gnueabihf \
-L${ARM_SYSROOT}/lib/arm-linux-gnueabihf \
-L${ARM_SYSROOT}/usr/lib/arm-linux-gnueabihf \
-no-pie -nostartfiles"

# ---- 6. Build ---------------------------------------------------------------
log "Cleaning previous build (may contain host/Pi objects)..."
make clean >/dev/null 2>&1 || true

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

# -lpthread/-ldl/-lrt are merged into libc only since glibc 2.34; on
# bullseye (2.31) they are separate shared libs and must be explicit. They are
# listed BEFORE -lao so libpthread is added as a command-line DSO (binutils
# --no-copy-dt-needed-entries: transitive deps of libao do not resolve object
# symbols). -lc is explicit so libc (2.31) resolves the GLIBC_PRIVATE symbols
# referenced by libdl/libpthread/libao before the driver appends its own -lc.
log "Cross-compiling with ${CROSS_CXX} (sysroot: ${ARM_SYSROOT})..."
make -j"$(nproc)" \
    CXX="${CROSS_CXX}" \
    CXXFLAGS_EXTRA="${SYSROOT_CPPFLAGS}" \
    BCM2835_INCLUDE="${ARM_SYSROOT}/include" \
    BCM2835_LIB="${SYSROOT_LDFLAGS} -L${ARM_SYSROOT}/lib -lbcm2835 -lmpg123 -lpthread -ldl -lrt -lao -lc" \
    CRT_BEGIN="${CRT1} ${CRTI} ${CRTBEGIN}" \
    CRT_END="${CRTEND} ${CRTN}"

# ---- 7. Verify --------------------------------------------------------------
log "Verifying (GLIBC requirement must be <= 2.31 to run on bullseye)..."
file bin/inmp441_rpi
GLIBC_NEEDED="$("${CROSS_OBJDUMP}" -T bin/inmp441_rpi 2>/dev/null | grep -oE 'GLIBC_[0-9.]+' | sort -Vu | tail -1 || true)"
log "max GLIBC version required: ${GLIBC_NEEDED:-unknown}"
log "Done. Copy to the Pi and run, e.g.:"
log "  scp bin/inmp441_rpi pi@<pi-ip>:/tmp/ && ssh pi@<pi-ip> 'sudo /tmp/inmp441_rpi --version'"
