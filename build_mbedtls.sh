#!/bin/bash
# Deprecated: mbedTLS build removed. Kept as stub for backward compatibility with old docs/scripts.
echo "[build_mbedtls.sh] mbedTLS backend removed. Use ./build_openssl.sh instead." >&2
exit 1

# Working directories
WORK_DIR="$(pwd)/libssh_build"
SOURCE_DIR="$WORK_DIR/libssh-$LIBSSH_VERSION"
BUILD_BASE_DIR="$WORK_DIR/build"
# Install directly into Android module prebuilt directory
INSTALL_BASE_DIR="$(pwd)/ssh-tunnel-android-app/app/src/main/prebuilt"

# Директории для mbedTLS
MBEDTLS_WORK_DIR="$WORK_DIR/mbedtls"
MBEDTLS_SOURCE_DIR="$MBEDTLS_WORK_DIR/mbedtls-$MBEDTLS_VERSION"
MBEDTLS_BUILD_BASE_DIR="$MBEDTLS_WORK_DIR/build"

echo "Creating work directories..."
mkdir -p "$WORK_DIR"
mkdir -p "$INSTALL_BASE_DIR"

# Download mbedTLS if not already present
if [ ! -f "$MBEDTLS_WORK_DIR/mbedtls-$MBEDTLS_VERSION.tar.gz" ]; then
    echo "Downloading mbedTLS $MBEDTLS_VERSION..."
    mkdir -p "$MBEDTLS_WORK_DIR"
    cd "$MBEDTLS_WORK_DIR"
    wget -O "mbedtls-$MBEDTLS_VERSION.tar.gz" "https://github.com/Mbed-TLS/mbedtls/archive/refs/tags/v$MBEDTLS_VERSION.tar.gz"
    tar -xf "mbedtls-$MBEDTLS_VERSION.tar.gz"
fi

# Download libssh if not already present
if [ ! -f "$WORK_DIR/libssh-$LIBSSH_VERSION.tar.xz" ]; then
    echo "Downloading libssh $LIBSSH_VERSION..."
    cd "$WORK_DIR"
    # Начиная с 0.11 используется каталог 0.11
    wget "https://www.libssh.org/files/0.11/libssh-$LIBSSH_VERSION.tar.xz"
    tar -xf "libssh-$LIBSSH_VERSION.tar.xz"
fi

# Build function per ABI
build_for_abi() {
    local ABI=$1
    echo "Building libssh for ABI: $ABI"
    
    # Настройка переменных для архитектуры
    case $ABI in
        "arm64-v8a")
            ANDROID_ABI="arm64-v8a"
            ARCH="aarch64"
            ;;
        "armeabi-v7a")
            ANDROID_ABI="armeabi-v7a"
            ARCH="arm"
            ;;
        "x86")
            ANDROID_ABI="x86"
            ARCH="i686"
            ;;
        "x86_64")
            ANDROID_ABI="x86_64"
            ARCH="x86_64"
            ;;
        *)
            echo "Unsupported ABI: $ABI"
            return 1
            ;;
    esac
    
    BUILD_DIR="$BUILD_BASE_DIR/$ABI"
    INSTALL_DIR="$INSTALL_BASE_DIR/libssh/$ABI"
    MBEDTLS_BUILD_DIR="$MBEDTLS_BUILD_BASE_DIR/$ABI"
    MBEDTLS_INSTALL_DIR="$INSTALL_BASE_DIR/mbedtls/$ABI"

    # Clean build directories to avoid stale CMakeCache conflicts
    rm -rf "$BUILD_DIR" "$MBEDTLS_BUILD_DIR"
    mkdir -p "$BUILD_DIR" "$INSTALL_DIR" "$MBEDTLS_BUILD_DIR" "$MBEDTLS_INSTALL_DIR"

    # 1) Build mbedTLS (static libraries)
    echo "Building mbedTLS for ABI: $ABI"
    cd "$MBEDTLS_BUILD_DIR"
    cmake "$MBEDTLS_SOURCE_DIR" \
        -DCMAKE_SYSTEM_NAME=Android \
        -DCMAKE_ANDROID_ARCH_ABI=$ANDROID_ABI \
        -DCMAKE_ANDROID_NDK="$NDK_PATH" \
        -DCMAKE_ANDROID_API=$MIN_API_LEVEL \
        -DCMAKE_ANDROID_STL_TYPE=c++_shared \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX="$MBEDTLS_INSTALL_DIR" \
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
        -DCMAKE_C_FLAGS="-fPIC" \
        -DCMAKE_CXX_FLAGS="-fPIC" \
        -DCMAKE_EXE_LINKER_FLAGS_INIT="$MAX_PAGE_LDFLAGS" \
        -DCMAKE_SHARED_LINKER_FLAGS_INIT="$MAX_PAGE_LDFLAGS" \
        -DCMAKE_MODULE_LINKER_FLAGS_INIT="$MAX_PAGE_LDFLAGS" \
        -DENABLE_TESTING=OFF \
        -DENABLE_PROGRAMS=OFF \
        -DBUILD_SHARED_LIBS=OFF
    cmake --build . --config Release -- -j$(nproc)
    cmake --install .
    echo "✅ mbedTLS for $ABI installed into $MBEDTLS_INSTALL_DIR"

    # 2) Build libssh linked against mbedTLS
    cd "$BUILD_DIR"
    
    # Android CMake configuration
    cmake "$SOURCE_DIR" \
        -DCMAKE_SYSTEM_NAME=Android \
        -DCMAKE_ANDROID_ARCH_ABI=$ANDROID_ABI \
        -DCMAKE_ANDROID_NDK="$NDK_PATH" \
        -DCMAKE_ANDROID_API=$MIN_API_LEVEL \
        -DCMAKE_ANDROID_STL_TYPE=c++_shared \
        -DCMAKE_C_FLAGS="-DS_IWRITE=S_IWUSR" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR" \
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
        -DCMAKE_EXE_LINKER_FLAGS_INIT="$MAX_PAGE_LDFLAGS" \
        -DCMAKE_SHARED_LINKER_FLAGS_INIT="$MAX_PAGE_LDFLAGS" \
        -DCMAKE_MODULE_LINKER_FLAGS_INIT="$MAX_PAGE_LDFLAGS" \
        -DWITH_EXAMPLES=OFF \
        -DWITH_TESTING=OFF \
        -DWITH_SERVER=OFF \
        -DWITH_ZLIB=OFF \
        -DWITH_GSSAPI=OFF \
        -DWITH_PCAP=OFF \
        -DWITH_SFTP=ON \
        -DWITH_MBEDTLS=ON \
        -DBUILD_SHARED_LIBS=OFF \
        -DMBEDTLS_ROOT_DIR="$MBEDTLS_INSTALL_DIR" \
        -DMBEDTLS_INCLUDE_DIR="$MBEDTLS_INSTALL_DIR/include" \
        -DMBEDTLS_CRYPTO_LIBRARY="$MBEDTLS_INSTALL_DIR/lib/libmbedcrypto.a" \
        -DMBEDTLS_X509_LIBRARY="$MBEDTLS_INSTALL_DIR/lib/libmbedx509.a" \
        -DMBEDTLS_SSL_LIBRARY="$MBEDTLS_INSTALL_DIR/lib/libmbedtls.a"
    
    # Build and install
    cmake --build . --config Release -- -j$(nproc)
    cmake --install .
    
    echo "✅ Build for $ABI completed"
}

# Build for all ABIs
for ABI in "${ABIS[@]}"; do
    build_for_abi "$ABI"
done

echo ""
echo "🎉 libssh + mbedTLS build completed for all ABIs!"
echo ""
echo "Libraries installed under: $INSTALL_BASE_DIR/libssh/"
echo ""
echo "File structure (first 20 libs):"
find "$INSTALL_BASE_DIR/libssh" -name "*.a" -o -name "*.so" | head -20

echo ""
echo "📁 Integration notes:"
echo "   - Include headers from: $INSTALL_DIR/include"
echo "   - Link static library:  $INSTALL_DIR/lib/libssh.a"
