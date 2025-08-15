#!/bin/bash

# Script to build OpenSSL 3.5 and libssh for Android
# Requires Android NDK and perl for building OpenSSL

set -e

# Check environment variables
if [ -z "$ANDROID_NDK_HOME" ] && [ -z "$ANDROID_NDK_ROOT" ]; then
    # Try to locate NDK automatically
    if [ -d "$HOME/Android/Sdk/ndk" ]; then
        export ANDROID_NDK_HOME="$HOME/Android/Sdk/ndk/$(ls $HOME/Android/Sdk/ndk/ | sort -V | tail -1)"
    elif [ -d "/opt/android-sdk/ndk" ]; then
        export ANDROID_NDK_HOME="/opt/android-sdk/ndk/$(ls /opt/android-sdk/ndk/ | sort -V | tail -1)"
    else
        echo "Error: ANDROID_NDK_HOME not set and NDK not found automatically"
        exit 1
    fi
fi

NDK_PATH="${ANDROID_NDK_HOME:-$ANDROID_NDK_ROOT}"
echo "Using Android NDK: $NDK_PATH"

# Check perl presence
if ! command -v perl &> /dev/null; then
    echo "Error: perl not found. perl is required to build OpenSSL."
    echo "Install perl: sudo apt-get install perl"
    exit 1
fi

# Build parameters
LIBSSH_VERSION="0.11.2"
OPENSSL_VERSION="3.5.0"
MIN_API_LEVEL=24

# Determine architectures to build
if [ -n "$ANDROID_ABI" ] && [ "$ANDROID_ABI" != "" ]; then
    # If ANDROID_ABI variable is set (e.g., in CI), build only that architecture
    ABIS=("$ANDROID_ABI")
    echo "🎯 Building for architecture from ANDROID_ABI: $ANDROID_ABI"
else
    # By default build all main architectures
    ABIS=("arm64-v8a" "armeabi-v7a" "x86_64" "x86")
    echo "🔄 Building for all architectures: ${ABIS[*]}"
fi

# Working directories
if [ -n "$ARCH_WORK_DIR" ]; then
    # Use isolated directory for CI/CD (each architecture in its own folder)
    WORK_DIR="$ARCH_WORK_DIR/openssl_build"
    echo "🔧 Using isolated working directory: $WORK_DIR"
else
    # Local build - use common directory
    WORK_DIR="$(pwd)/openssl_build"
fi
SOURCE_DIR="$WORK_DIR/libssh-$LIBSSH_VERSION"
BUILD_BASE_DIR="$WORK_DIR/build"
# Install directly into Android module prebuilts
INSTALL_BASE_DIR="$(pwd)/ssh-tunnel-android-app/app/src/main/prebuilt"

# Directories for OpenSSL
OPENSSL_WORK_DIR="$WORK_DIR/openssl"
OPENSSL_SOURCE_DIR="$OPENSSL_WORK_DIR/openssl-$OPENSSL_VERSION"
OPENSSL_BUILD_BASE_DIR="$OPENSSL_WORK_DIR/build"

echo "Creating working directories..."
mkdir -p "$WORK_DIR"
mkdir -p "$INSTALL_BASE_DIR"

# Function to download with mirrors
download_with_mirrors() {
    local filename=$1
    local target_dir=$2
    local primary_url=$3
    shift 3
    local mirror_urls=("$@")
    
    cd "$target_dir"
    
    if [ -f "$filename" ]; then
        echo "✅ $filename already exists"
        return 0
    fi
    
    echo "⬇️ Downloading $filename..."
    
    # Try primary URL
    if wget -O "$filename.tmp" "$primary_url" 2>/dev/null; then
        mv "$filename.tmp" "$filename"
        echo "✅ Downloaded from primary source: $primary_url"
        return 0
    fi
    
    # Try mirrors
    for mirror_url in "${mirror_urls[@]}"; do
        echo "🔄 Trying mirror: $mirror_url"
        if wget -O "$filename.tmp" "$mirror_url" 2>/dev/null; then
            mv "$filename.tmp" "$filename"
            echo "✅ Downloaded from mirror: $mirror_url"
            return 0
        fi
    done
    
    echo "❌ Failed to download $filename"
    rm -f "$filename.tmp"
    return 1
}

# Download OpenSSL if not yet downloaded (with locking for CI)
if [ ! -f "$OPENSSL_WORK_DIR/openssl-$OPENSSL_VERSION.tar.gz" ]; then
    echo "Downloading OpenSSL $OPENSSL_VERSION..."
    mkdir -p "$OPENSSL_WORK_DIR"
    cd "$OPENSSL_WORK_DIR"
    
    # Check cached files from CI
    if [ -n "$SOURCE_CACHE_DIR" ] && [ -f "$SOURCE_CACHE_DIR/openssl-$OPENSSL_VERSION.tar.gz" ]; then
        echo "📦 Using cached OpenSSL from $SOURCE_CACHE_DIR"
        cp "$SOURCE_CACHE_DIR/openssl-$OPENSSL_VERSION.tar.gz" .
        tar -xf "openssl-$OPENSSL_VERSION.tar.gz"
    # Check pre-downloaded files from old CI
    elif [ -f "/tmp/source_cache/openssl-$OPENSSL_VERSION.tar.gz" ]; then
        echo "📦 Using pre-downloaded OpenSSL from cache"
        cp "/tmp/source_cache/openssl-$OPENSSL_VERSION.tar.gz" .
        tar -xf "openssl-$OPENSSL_VERSION.tar.gz"
    # In CI use architecture-based locking
    elif [ -n "$ANDROID_ABI" ] && [ -n "$ARCH_WORK_DIR" ]; then
        LOCK_FILE="/tmp/openssl_download_${ANDROID_ABI}.lock"
        (
            # Acquire exclusive lock for up to 300 seconds
            flock -x -w 300 200
            if [ ! -f "openssl-$OPENSSL_VERSION.tar.gz" ]; then
                echo "🔒 Lock acquired for $ANDROID_ABI, downloading OpenSSL..."
                download_with_mirrors "openssl-$OPENSSL_VERSION.tar.gz" "." \
                    "https://github.com/openssl/openssl/releases/download/openssl-$OPENSSL_VERSION/openssl-$OPENSSL_VERSION.tar.gz" \
                    "https://www.openssl.org/source/openssl-$OPENSSL_VERSION.tar.gz" \
                    "https://ftp.openssl.org/source/openssl-$OPENSSL_VERSION.tar.gz" \
                    "https://mirror.yandex.ru/pub/OpenSSL/openssl-$OPENSSL_VERSION.tar.gz"
                tar -xf "openssl-$OPENSSL_VERSION.tar.gz"
            else
                echo "📁 OpenSSL already downloaded by another process"
            fi
        ) 200>"$LOCK_FILE"
    else
        # Local build or fallback - download with mirrors
        download_with_mirrors "openssl-$OPENSSL_VERSION.tar.gz" "$OPENSSL_WORK_DIR" \
            "https://github.com/openssl/openssl/releases/download/openssl-$OPENSSL_VERSION/openssl-$OPENSSL_VERSION.tar.gz" \
            "https://www.openssl.org/source/openssl-$OPENSSL_VERSION.tar.gz" \
            "https://ftp.openssl.org/source/openssl-$OPENSSL_VERSION.tar.gz" \
            "https://mirror.yandex.ru/pub/OpenSSL/openssl-$OPENSSL_VERSION.tar.gz"
        tar -xf "openssl-$OPENSSL_VERSION.tar.gz"
    fi
fi

# Download libssh if not yet downloaded (with locking for CI)
if [ ! -f "$WORK_DIR/libssh-$LIBSSH_VERSION.tar.xz" ]; then
    echo "Downloading libssh $LIBSSH_VERSION..."
    cd "$WORK_DIR"
    
    # Check cached files from CI
    if [ -n "$SOURCE_CACHE_DIR" ] && [ -f "$SOURCE_CACHE_DIR/libssh-$LIBSSH_VERSION.tar.xz" ]; then
        echo "📦 Using cached libssh from $SOURCE_CACHE_DIR"
        cp "$SOURCE_CACHE_DIR/libssh-$LIBSSH_VERSION.tar.xz" .
        tar -xf "libssh-$LIBSSH_VERSION.tar.xz"
    # Check pre-downloaded files from old CI
    elif [ -f "/tmp/source_cache/libssh-$LIBSSH_VERSION.tar.xz" ]; then
        echo "📦 Using pre-downloaded libssh from cache"
        cp "/tmp/source_cache/libssh-$LIBSSH_VERSION.tar.xz" .
        tar -xf "libssh-$LIBSSH_VERSION.tar.xz"
    # In CI use architecture-based locking
    elif [ -n "$ANDROID_ABI" ] && [ -n "$ARCH_WORK_DIR" ]; then
        LOCK_FILE="/tmp/libssh_download_${ANDROID_ABI}.lock"
        (
            # Acquire exclusive lock for up to 300 seconds
            flock -x -w 300 200
            if [ ! -f "libssh-$LIBSSH_VERSION.tar.xz" ]; then
                echo "🔒 Lock acquired for $ANDROID_ABI, downloading libssh..."
                download_with_mirrors "libssh-$LIBSSH_VERSION.tar.xz" "." \
                    "https://www.libssh.org/files/0.11/libssh-$LIBSSH_VERSION.tar.xz" \
                    "https://git.libssh.org/projects/libssh.git/snapshot/libssh-$LIBSSH_VERSION.tar.xz" \
                    "https://mirror.yandex.ru/debian/pool/main/libs/libssh/libssh_$LIBSSH_VERSION.orig.tar.xz"
                tar -xf "libssh-$LIBSSH_VERSION.tar.xz"
            else
                echo "📁 libssh already downloaded by another process"
            fi
        ) 200>"$LOCK_FILE"
    else
        # Local build or fallback - download with mirrors
        download_with_mirrors "libssh-$LIBSSH_VERSION.tar.xz" "$WORK_DIR" \
            "https://www.libssh.org/files/0.11/libssh-$LIBSSH_VERSION.tar.xz" \
            "https://git.libssh.org/projects/libssh.git/snapshot/libssh-$LIBSSH_VERSION.tar.xz" \
            "https://mirror.yandex.ru/debian/pool/main/libs/libssh/libssh_$LIBSSH_VERSION.orig.tar.xz"
        tar -xf "libssh-$LIBSSH_VERSION.tar.xz"
    fi
fi

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

# Build for single architecture
build_for_abi() {
    local ABI=$1
    echo "Building OpenSSL and libssh for architecture: $ABI"
    
    # Get flags for architecture
    local ARCH_FLAGS=$(get_arch_flags "$ABI")
    if [ $? -ne 0 ]; then
    echo "Error getting flags for $ABI"
        return 1
    fi
    
    local ARCH_CFLAGS=$(echo "$ARCH_FLAGS" | cut -d'|' -f1)
    local ARCH_LDFLAGS=$(echo "$ARCH_FLAGS" | cut -d'|' -f2)
    
    echo "Flags for $ABI:"
    echo "  CFLAGS: $ARCH_CFLAGS"
    echo "  LDFLAGS: $ARCH_LDFLAGS"
    
    # Setup variables for ABI
    case $ABI in
        "arm64-v8a")
            ANDROID_ABI="arm64-v8a"
            ARCH="aarch64"
            OPENSSL_ARCH="android-arm64"
            TOOLCHAIN_PREFIX="aarch64-linux-android"
            ;;
        "armeabi-v7a")
            ANDROID_ABI="armeabi-v7a"
            ARCH="arm"
            OPENSSL_ARCH="android-arm"
            TOOLCHAIN_PREFIX="armv7a-linux-androideabi"
            ;;
        "x86")
            ANDROID_ABI="x86"
            ARCH="i686"
            OPENSSL_ARCH="android-x86"
            TOOLCHAIN_PREFIX="i686-linux-android"
            ;;
        "x86_64")
            ANDROID_ABI="x86_64"
            ARCH="x86_64"
            OPENSSL_ARCH="android-x86_64"
            TOOLCHAIN_PREFIX="x86_64-linux-android"
            ;;
        *)
            echo "Unsupported architecture: $ABI"
            return 1
            ;;
    esac
    
    BUILD_DIR="$BUILD_BASE_DIR/$ABI"
    INSTALL_DIR="$INSTALL_BASE_DIR/libssh/$ABI"
    OPENSSL_BUILD_DIR="$OPENSSL_BUILD_BASE_DIR/$ABI"
    OPENSSL_INSTALL_DIR="$INSTALL_BASE_DIR/openssl/$ABI"

    # Android toolchain path
    TOOLCHAIN="$NDK_PATH/toolchains/llvm/prebuilt/linux-x86_64"
    
    # Ensure toolchain exists
    if [ ! -d "$TOOLCHAIN" ]; then
    echo "Error: Android toolchain not found at $TOOLCHAIN"
        exit 1
    fi
    
    # Full flags
    FULL_CFLAGS="-DS_IWRITE=S_IWUSR $ARCH_CFLAGS"
    FULL_LDFLAGS="$ARCH_LDFLAGS"
    
    export AR="$TOOLCHAIN/bin/llvm-ar"
    export CC="$TOOLCHAIN/bin/${TOOLCHAIN_PREFIX}${MIN_API_LEVEL}-clang"
    export CXX="$TOOLCHAIN/bin/${TOOLCHAIN_PREFIX}${MIN_API_LEVEL}-clang++"
    export ASM="$CC"
    export STRIP="$TOOLCHAIN/bin/llvm-strip"
    export RANLIB="$TOOLCHAIN/bin/llvm-ranlib"
    export PATH="$TOOLCHAIN/bin:$PATH"
    
    # Export flags
    export CFLAGS="$FULL_CFLAGS"
    export CXXFLAGS="$FULL_CFLAGS"
    export LDFLAGS="$FULL_LDFLAGS"
    
    # Extra vars for OpenSSL
    export ANDROID_NDK_ROOT="$NDK_PATH"
    export CROSS_COMPILE=""
    
    echo "Environment configured for $ABI:"
    echo "  CC: $CC"
    echo "  CFLAGS: $CFLAGS"
    echo "  LDFLAGS: $LDFLAGS"
    echo "  Toolchain: $TOOLCHAIN"

    # Clean build directories
    rm -rf "$BUILD_DIR" "$OPENSSL_BUILD_DIR"
    mkdir -p "$BUILD_DIR" "$INSTALL_DIR" "$OPENSSL_BUILD_DIR" "$OPENSSL_INSTALL_DIR"

    # 1) Build OpenSSL (static)
    echo "Building OpenSSL for: $ABI"
    
    # Copy OpenSSL sources into build dir
    OPENSSL_BUILD_SOURCE="$OPENSSL_BUILD_DIR/openssl-$OPENSSL_VERSION"
    rm -rf "$OPENSSL_BUILD_SOURCE"
    cp -r "$OPENSSL_SOURCE_DIR" "$OPENSSL_BUILD_SOURCE"
    cd "$OPENSSL_BUILD_SOURCE"
    
    # Clean previous config
    if [ -f Makefile ]; then
        make clean || true
    fi
    
    # Configure OpenSSL for Android
    echo "Configuring OpenSSL with:"
    echo "  Target: $OPENSSL_ARCH"
    echo "  API Level: $MIN_API_LEVEL"
    echo "  Install Dir: $OPENSSL_INSTALL_DIR"
    
    # Special settings for ARMv7/x86/x86_64 to avoid link issues
    if [ "$ABI" = "armeabi-v7a" ] || [ "$ABI" = "x86_64" ] || [ "$ABI" = "x86" ]; then
    echo "Applying special settings for $ABI..."
        ./Configure $OPENSSL_ARCH \
            -D__ANDROID_API__=$MIN_API_LEVEL \
            --prefix="$OPENSSL_INSTALL_DIR" \
            --openssldir="$OPENSSL_INSTALL_DIR" \
            no-shared \
            no-tests \
            no-ui-console \
            no-docs \
            no-apps \
            no-asm \
            -static
    else
        ./Configure $OPENSSL_ARCH \
            -D__ANDROID_API__=$MIN_API_LEVEL \
            --prefix="$OPENSSL_INSTALL_DIR" \
            --openssldir="$OPENSSL_INSTALL_DIR" \
            no-shared \
            no-tests \
            no-ui-console \
            no-docs \
            -static
    fi
    
    # Ensure Configure succeeded
    if [ ! -f Makefile ]; then
    echo "Error: OpenSSL Configure did not create Makefile"
    echo "Check logs above for details"
        exit 1
    fi
    
    # Build
    echo "Compiling OpenSSL..."
    if [ "$ABI" = "armeabi-v7a" ] || [ "$ABI" = "x86_64" ] || [ "$ABI" = "x86" ]; then
    # For ARMv7/x86/x86_64 build libs only (no apps)
    echo "Building libraries only for $ABI..."
        make -j$(nproc) build_libs
    else
        make -j$(nproc)
    fi
    
    echo "Installing OpenSSL..."
    if [ "$ABI" = "armeabi-v7a" ] || [ "$ABI" = "x86_64" ] || [ "$ABI" = "x86" ]; then
    # For ARMv7/x86/x86_64 install only required parts
    echo "Installing libs and headers for $ABI..."
        make install_ssldirs install_dev
        # Verify and install additionally if needed
        if [ ! -f "$OPENSSL_INSTALL_DIR/include/openssl/des.h" ]; then
            echo "Force copying headers..."
            # Copy header files directly
            mkdir -p "$OPENSSL_INSTALL_DIR/include/openssl"
            cp -r include/openssl/* "$OPENSSL_INSTALL_DIR/include/openssl/" 2>/dev/null || true
            cp -r include/crypto/* "$OPENSSL_INSTALL_DIR/include/openssl/" 2>/dev/null || true
        fi
    else
        make install_sw
    fi
    
    # Verify result
    if [ ! -f "$OPENSSL_INSTALL_DIR/lib/libssl.a" ] || [ ! -f "$OPENSSL_INSTALL_DIR/lib/libcrypto.a" ]; then
    echo "Error: OpenSSL libraries missing after install"
    echo "Expected: $OPENSSL_INSTALL_DIR/lib/libssl.a and libcrypto.a"
        exit 1
    fi
    
    echo "✅ OpenSSL for $ABI installed at $OPENSSL_INSTALL_DIR"

    # 2) Build libssh with OpenSSL linkage
    echo "Building libssh for: $ABI"
    cd "$BUILD_DIR"
    
    # Ensure OpenSSL libs available
    if [ ! -f "$OPENSSL_INSTALL_DIR/lib/libssl.a" ] || [ ! -f "$OPENSSL_INSTALL_DIR/lib/libcrypto.a" ]; then
    echo "Error: OpenSSL libs not found for libssh build"
        exit 1
    fi
    
    echo "Configuring libssh with:"
    echo "  OpenSSL Root: $OPENSSL_INSTALL_DIR"
    echo "  Install Dir: $INSTALL_DIR"
    echo "  ABI: $ANDROID_ABI"
    
    # CMake configure for Android
    cmake "$SOURCE_DIR" \
        -DCMAKE_SYSTEM_NAME=Android \
        -DCMAKE_ANDROID_ARCH_ABI=$ANDROID_ABI \
        -DCMAKE_ANDROID_NDK="$NDK_PATH" \
        -DCMAKE_ANDROID_API=$MIN_API_LEVEL \
        -DCMAKE_ANDROID_STL_TYPE=c++_shared \
        -DCMAKE_C_FLAGS="$FULL_CFLAGS" \
        -DCMAKE_CXX_FLAGS="$FULL_CFLAGS" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR" \
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
        -DCMAKE_EXE_LINKER_FLAGS_INIT="$FULL_LDFLAGS" \
        -DCMAKE_SHARED_LINKER_FLAGS_INIT="$FULL_LDFLAGS" \
        -DCMAKE_MODULE_LINKER_FLAGS_INIT="$FULL_LDFLAGS" \
        -DWITH_EXAMPLES=OFF \
        -DWITH_TESTING=OFF \
        -DWITH_SERVER=OFF \
        -DWITH_ZLIB=OFF \
        -DWITH_GSSAPI=OFF \
        -DWITH_PCAP=OFF \
        -DWITH_SFTP=ON \
        -DWITH_OPENSSL=ON \
        -DWITH_MBEDTLS=OFF \
        -DBUILD_SHARED_LIBS=OFF \
        -DOPENSSL_ROOT_DIR="$OPENSSL_INSTALL_DIR" \
        -DOPENSSL_INCLUDE_DIR="$OPENSSL_INSTALL_DIR/include" \
        -DOPENSSL_CRYPTO_LIBRARY="$OPENSSL_INSTALL_DIR/lib/libcrypto.a" \
        -DOPENSSL_SSL_LIBRARY="$OPENSSL_INSTALL_DIR/lib/libssl.a"
    
    # Check CMake result
    if [ $? -ne 0 ]; then
    echo "Error: CMake configure for libssh failed"
        exit 1
    fi
    
    # Build libssh
    echo "Compiling libssh..."
    cmake --build . --config Release -- -j$(nproc)
    
    if [ $? -ne 0 ]; then
    echo "Error: libssh compilation failed"
        exit 1
    fi
    
    echo "Installing libssh..."
    cmake --install .
    
    # Verify libssh
    if [ ! -f "$INSTALL_DIR/lib/libssh.a" ]; then
    echo "Error: libssh library not found after install"
    echo "Expected: $INSTALL_DIR/lib/libssh.a"
        exit 1
    fi
    
    echo "✅ Build for $ABI finished"
}

# Build all architectures
for ABI in "${ABIS[@]}"; do
    build_for_abi "$ABI"
done

echo ""
echo "🎉 Build of libssh with OpenSSL finished for all architectures!"
echo ""
echo "Libraries installed at:"
echo "  - libssh: $INSTALL_BASE_DIR/libssh/"
echo "  - OpenSSL: $INSTALL_BASE_DIR/openssl/"
echo ""
echo "File structure:"
find "$INSTALL_BASE_DIR/libssh" -name "*.a" -o -name "*.so" | head -10
echo ""
find "$INSTALL_BASE_DIR/openssl" -name "*.a" -o -name "*.so" | head -10

echo ""
echo "📁 Update CMakeLists.txt to use:"
echo "   - libssh headers: \$INSTALL_DIR/libssh/\$ABI/include"
echo "   - libssh library: \$INSTALL_DIR/libssh/\$ABI/lib/libssh.a"
echo "   - OpenSSL headers: \$INSTALL_DIR/openssl/\$ABI/include"
echo "   - OpenSSL libs: \$INSTALL_DIR/openssl/\$ABI/lib/libssl.a and libcrypto.a"
