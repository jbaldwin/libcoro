#!/usr/bin/env bash

# OpenSSL builder for Android ABIs
# - Per-ABI compiler/linker flags via get_arch_flags()
# - Installs into external/openssl/<ABI>
# - Downloads OpenSSL sources automatically

set -euo pipefail

OPENSSL_VERSION="${OPENSSL_VERSION:-3.5.0}"
MIN_API_LEVEL="${MIN_API_LEVEL:-24}"

info(){ echo -e "[INFO] $*"; }
err(){ echo -e "[ERR ] $*" >&2; }

usage(){ cat <<EOF
Usage: $0 [options]
  --abis "a;b;c"      ABIs to build (default: arm64-v8a;armeabi-v7a;x86_64;x86 or ANDROID_ABI)
  --version X.Y.Z     OpenSSL version (default ${OPENSSL_VERSION})
  --api N             Android API level (default ${MIN_API_LEVEL})
    --clean             Remove build cache before building

Environment (optional):

Examples:
  ANDROID_ABI=arm64-v8a $0
  $0 --abis "arm64-v8a;x86_64" --api 26
EOF
}

ABIS_ARG=""
CLEAN=0
while [[ $# -gt 0 ]]; do
    case "$1" in
        --abis) ABIS_ARG="$2"; shift 2;;
        --version) OPENSSL_VERSION="$2"; shift 2;;
        --api) MIN_API_LEVEL="$2"; shift 2;;
        --clean) CLEAN=1; shift;;
        -h|--help) usage; exit 0;;
        *) err "Unknown arg $1"; usage; exit 1;;
    esac
done

# NDK discovery
if [[ -z "${ANDROID_NDK_HOME:-}" && -z "${ANDROID_NDK_ROOT:-}" ]]; then
    if [[ -d /opt/android-sdk/ndk ]]; then
        export ANDROID_NDK_HOME="/opt/android-sdk/ndk/$(ls /opt/android-sdk/ndk | sort -V | tail -1)"
    elif [[ -d $HOME/Android/Sdk/ndk ]]; then
        export ANDROID_NDK_HOME="$HOME/Android/Sdk/ndk/$(ls $HOME/Android/Sdk/ndk | sort -V | tail -1)"
    else
        err "Android NDK not found; set ANDROID_NDK_HOME"; exit 1
    fi
fi
NDK_PATH="${ANDROID_NDK_HOME:-$ANDROID_NDK_ROOT}"
info "Using NDK: $NDK_PATH"

command -v perl >/dev/null || { err "perl required (sudo apt-get install perl)"; exit 1; }
command -v curl >/dev/null || { err "curl required"; exit 1; }

if [[ -n "${ANDROID_ABI:-}" && -z "$ABIS_ARG" ]]; then
    ABIS=("${ANDROID_ABI}")
elif [[ -n "$ABIS_ARG" ]]; then
    IFS=';' read -r -a ABIS <<<"$ABIS_ARG"
else
    ABIS=(arm64-v8a armeabi-v7a x86_64 x86)
fi
info "Target ABIs: ${ABIS[*]}"

ROOT_DIR="$(pwd)"

# Cache and paths
CACHE_DIR="$ROOT_DIR/.build_cache"
OPENSSL_SRC_DIR="$CACHE_DIR/src"
OPENSSL_BUILD_BASE_DIR="$CACHE_DIR/build/openssl"
INSTALL_BASE_DIR="$ROOT_DIR/external"
OPENSSL_INSTALL_ROOT="$INSTALL_BASE_DIR/openssl"
TARBALL="openssl-${OPENSSL_VERSION}.tar.gz"

[[ $CLEAN -eq 1 ]] && { info "Cleaning cache"; rm -rf "$CACHE_DIR"; }
mkdir -p "$OPENSSL_SRC_DIR" "$OPENSSL_BUILD_BASE_DIR" "$OPENSSL_INSTALL_ROOT"

download_openssl() {
    local out="$CACHE_DIR/$TARBALL"
    if [[ -f $out ]]; then
        info "Tarball cached: $out"; return 0; fi
    local urls=(
        "https://github.com/openssl/openssl/releases/download/openssl-${OPENSSL_VERSION}/openssl-${OPENSSL_VERSION}.tar.gz"
        "https://www.openssl.org/source/openssl-${OPENSSL_VERSION}.tar.gz"
        "https://ftp.openssl.org/source/openssl-${OPENSSL_VERSION}.tar.gz"
        "https://mirror.yandex.ru/pub/OpenSSL/openssl-${OPENSSL_VERSION}.tar.gz"
    )
    info "Downloading OpenSSL ${OPENSSL_VERSION}";
    for u in "${urls[@]}"; do
        info " -> $u"
        if curl -fsSL "$u" -o "$out.tmp"; then
            mv "$out.tmp" "$out"; info "Downloaded"; return 0; fi
    done
    err "Failed to download OpenSSL tarball"; return 1
}

extract_openssl() {
    local dst="$OPENSSL_SRC_DIR/openssl-${OPENSSL_VERSION}"
    [[ -d $dst ]] && { info "Sources already extracted"; return 0; }
    tar -xf "$CACHE_DIR/$TARBALL" -C "$OPENSSL_SRC_DIR"
}

# Get compile flags for specific architecture
get_arch_flags() {
    local ABI=$1
    local CFLAGS=""
    local LDFLAGS=""

    case $ABI in
        "arm64-v8a")
            # ARM64 flags - 16K page support for Android 15+
            CFLAGS="-march=armv8-a -fPIC"
            LDFLAGS="-Wl,-z,max-page-size=16384 -Wl,-z,common-page-size=16384"
            ;;
        "armeabi-v7a")
            # ARMv7 flags - thumb + NEON
            CFLAGS="-march=armv7-a -mfloat-abi=softfp -mfpu=neon -mthumb -fPIC"
            LDFLAGS="-Wl,--fix-cortex-a8"
            ;;
        "x86")
            # x86 flags - SSE support
            CFLAGS="-march=i686 -msse3 -fPIC"
            LDFLAGS=""
            ;;
        "x86_64")
            # x86_64 flags - optimized for modern CPUs
            CFLAGS="-march=x86-64 -msse4.2 -mpopcnt -m64 -fPIC -Wno-macro-redefined"
            LDFLAGS=""
            ;;
        *)
            echo "Unsupported architecture: $ABI"
            return 1
            ;;
    esac

    echo "$CFLAGS|$LDFLAGS"
}

# Build for single architecture (OpenSSL only)
build_for_abi() {
    local ABI=$1
    info "Building for ABI: $ABI"

    # Get flags
    local ARCH_FLAGS
    if ! ARCH_FLAGS=$(get_arch_flags "$ABI"); then
        err "Error getting flags for $ABI"; return 1
    fi
    local ARCH_CFLAGS ARCH_LDFLAGS
    ARCH_CFLAGS=$(echo "$ARCH_FLAGS" | cut -d'|' -f1)
    ARCH_LDFLAGS=$(echo "$ARCH_FLAGS" | cut -d'|' -f2)
    info "CFLAGS: $ARCH_CFLAGS"
    info "LDFLAGS: $ARCH_LDFLAGS"

    # ABI specifics
    local ANDROID_ABI ARCH OPENSSL_ARCH TOOLCHAIN_PREFIX
    case $ABI in
        "arm64-v8a")
            ANDROID_ABI="arm64-v8a"; ARCH="aarch64"; OPENSSL_ARCH="android-arm64"; TOOLCHAIN_PREFIX="aarch64-linux-android";;
        "armeabi-v7a")
            ANDROID_ABI="armeabi-v7a"; ARCH="arm"; OPENSSL_ARCH="android-arm"; TOOLCHAIN_PREFIX="armv7a-linux-androideabi";;
        "x86")
            ANDROID_ABI="x86"; ARCH="i686"; OPENSSL_ARCH="android-x86"; TOOLCHAIN_PREFIX="i686-linux-android";;
        "x86_64")
            ANDROID_ABI="x86_64"; ARCH="x86_64"; OPENSSL_ARCH="android-x86_64"; TOOLCHAIN_PREFIX="x86_64-linux-android";;
        *) err "Unsupported architecture: $ABI"; return 1;;
    esac

    local OPENSSL_BUILD_DIR="$OPENSSL_BUILD_BASE_DIR/$ABI"
    local OPENSSL_INSTALL_DIR="$OPENSSL_INSTALL_ROOT/$ABI"

    local TOOLCHAIN="$NDK_PATH/toolchains/llvm/prebuilt/linux-x86_64"
    [[ -d "$TOOLCHAIN" ]] || { err "Android toolchain not found at $TOOLCHAIN"; exit 1; }

    local FULL_CFLAGS="-DS_IWRITE=S_IWUSR $ARCH_CFLAGS"
    local FULL_LDFLAGS="$ARCH_LDFLAGS"

    export AR="$TOOLCHAIN/bin/llvm-ar"
    export CC="$TOOLCHAIN/bin/${TOOLCHAIN_PREFIX}${MIN_API_LEVEL}-clang"
    export CXX="$TOOLCHAIN/bin/${TOOLCHAIN_PREFIX}${MIN_API_LEVEL}-clang++"
    export ASM="$CC"
    export STRIP="$TOOLCHAIN/bin/llvm-strip"
    export RANLIB="$TOOLCHAIN/bin/llvm-ranlib"
    export PATH="$TOOLCHAIN/bin:$PATH"

    export CFLAGS="$FULL_CFLAGS"
    export CXXFLAGS="$FULL_CFLAGS"
    export LDFLAGS="$FULL_LDFLAGS"
    export ANDROID_NDK_ROOT="$NDK_PATH"
    export CROSS_COMPILE=""

    info "Environment configured for $ABI: CC=$CC"

    # Prepare dirs
    rm -rf "$OPENSSL_BUILD_DIR"
    mkdir -p "$OPENSSL_BUILD_DIR" "$OPENSSL_INSTALL_DIR"

    # Build OpenSSL (static)
    info "Building OpenSSL for: $ABI"
    local OPENSSL_SOURCE_DIR="$OPENSSL_SRC_DIR/openssl-$OPENSSL_VERSION"
    local OPENSSL_BUILD_SOURCE="$OPENSSL_BUILD_DIR/openssl-$OPENSSL_VERSION"
    rm -rf "$OPENSSL_BUILD_SOURCE"
    cp -r "$OPENSSL_SOURCE_DIR" "$OPENSSL_BUILD_SOURCE"
    pushd "$OPENSSL_BUILD_SOURCE" >/dev/null
    if [[ -f Makefile ]]; then make clean || true; fi

    info "Configuring OpenSSL target=$OPENSSL_ARCH api=$MIN_API_LEVEL"
    if [[ "$ABI" == "armeabi-v7a" || "$ABI" == "x86_64" || "$ABI" == "x86" ]]; then
        ./Configure "$OPENSSL_ARCH" \
            -D__ANDROID_API__=$MIN_API_LEVEL \
            --prefix="$OPENSSL_INSTALL_DIR" \
            --openssldir="$OPENSSL_INSTALL_DIR" \
            no-shared no-tests no-ui-console no-docs no-apps no-asm -static
    else
        ./Configure "$OPENSSL_ARCH" \
            -D__ANDROID_API__=$MIN_API_LEVEL \
            --prefix="$OPENSSL_INSTALL_DIR" \
            --openssldir="$OPENSSL_INSTALL_DIR" \
            no-shared no-tests no-ui-console no-docs -static
    fi
    [[ -f Makefile ]] || { err "OpenSSL Configure did not create Makefile ($ABI)"; exit 1; }

    info "Compiling OpenSSL..."
    if [[ "$ABI" == "armeabi-v7a" || "$ABI" == "x86_64" || "$ABI" == "x86" ]]; then
        make -j"$(nproc)" build_libs
    else
        make -j"$(nproc)"
    fi

    info "Installing OpenSSL..."
    if [[ "$ABI" == "armeabi-v7a" || "$ABI" == "x86_64" || "$ABI" == "x86" ]]; then
        make install_ssldirs install_dev
        if [[ ! -f "$OPENSSL_INSTALL_DIR/include/openssl/des.h" ]]; then
            info "Force copying headers..."
            mkdir -p "$OPENSSL_INSTALL_DIR/include/openssl"
            cp -r include/openssl/* "$OPENSSL_INSTALL_DIR/include/openssl/" 2>/dev/null || true
            cp -r include/crypto/* "$OPENSSL_INSTALL_DIR/include/openssl/" 2>/dev/null || true
        fi
    else
        make install_sw
    fi
    [[ -f "$OPENSSL_INSTALL_DIR/lib/libssl.a" && -f "$OPENSSL_INSTALL_DIR/lib/libcrypto.a" ]] || { err "OpenSSL libraries missing ($ABI)"; exit 1; }
    popd >/dev/null
    info "OpenSSL for $ABI installed at $OPENSSL_INSTALL_DIR"

    info "✅ Finished $ABI"
}

# Prepare OpenSSL sources
download_openssl
extract_openssl

# Build all ABIs
for ABI in "${ABIS[@]}"; do
    build_for_abi "$ABI"
done

echo
echo "Summary (OpenSSL):"
for ABI in "${ABIS[@]}"; do
    echo "  $OPENSSL_INSTALL_ROOT/$ABI/lib:"; ls -1 "$OPENSSL_INSTALL_ROOT/$ABI/lib" 2>/dev/null || true
done
echo
echo "Done. Set OPENSSL_ROOT_DIR to ${OPENSSL_INSTALL_ROOT}/<ABI> when configuring CMake."
