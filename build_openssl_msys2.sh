#!/bin/bash

# OpenSSL 3.3.2 + libssh build script for Android using MSYS2
# This script provides full Unix compatibility for OpenSSL Configure

set -e

echo "[INFO] Building OpenSSL 3.3.2 and libssh for Android using MSYS2..."

# Configuration
ANDROID_NDK_HOME="/e/Android/Sdk/ndk/25.1.8937393"
OPENSSL_VERSION="3.3.2"
LIBSSH_VERSION="0.11.2"
MIN_API_LEVEL="24"
ABI="arm64-v8a"
OPENSSL_ARCH="android-arm64"
TOOLCHAIN_PREFIX="aarch64-linux-android"

# Directories (convert Windows paths to MSYS2 format)
REPO_DIR="$(cd "$(dirname "$0")" && pwd)"
WORK_DIR="$REPO_DIR/openssl_build"
OPENSSL_WORK_DIR="$WORK_DIR/openssl"
OPENSSL_SOURCE_DIR="$OPENSSL_WORK_DIR/openssl-$OPENSSL_VERSION"
LIBSSH_SOURCE_DIR="$WORK_DIR/libssh-$LIBSSH_VERSION"
PREBUILT_DIR="$REPO_DIR/ssh-tunnel-android-app/app/src/main/prebuilt"
OPENSSL_INSTALL_DIR="$PREBUILT_DIR/openssl/$ABI"
LIBSSH_INSTALL_DIR="$PREBUILT_DIR/libssh/$ABI"

# Create directories
mkdir -p "$WORK_DIR" "$PREBUILT_DIR" "$OPENSSL_INSTALL_DIR" "$LIBSSH_INSTALL_DIR"

# Download OpenSSL if needed
if [ ! -f "$OPENSSL_WORK_DIR/openssl-$OPENSSL_VERSION.tar.gz" ]; then
    echo "[INFO] Downloading OpenSSL $OPENSSL_VERSION..."
    mkdir -p "$OPENSSL_WORK_DIR"
    cd "$OPENSSL_WORK_DIR"
    curl -L -o "openssl-$OPENSSL_VERSION.tar.gz" \
        "https://www.openssl.org/source/openssl-$OPENSSL_VERSION.tar.gz"
    tar -xf "openssl-$OPENSSL_VERSION.tar.gz"
fi

# Download libssh if needed
if [ ! -f "$WORK_DIR/libssh-$LIBSSH_VERSION.tar.xz" ]; then
    echo "[INFO] Downloading libssh $LIBSSH_VERSION..."
    cd "$WORK_DIR"
    curl -L -o "libssh-$LIBSSH_VERSION.tar.xz" \
        "https://www.libssh.org/files/0.11/libssh-$LIBSSH_VERSION.tar.xz"
    tar -xf "libssh-$LIBSSH_VERSION.tar.xz"
fi

# Setup toolchain paths
TOOLCHAIN_BIN="$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/windows-x86_64/bin"

# Environment variables for cross-compilation
export ANDROID_NDK_ROOT="$ANDROID_NDK_HOME"
export CC="$TOOLCHAIN_BIN/${TOOLCHAIN_PREFIX}${MIN_API_LEVEL}-clang"
export CXX="$TOOLCHAIN_BIN/${TOOLCHAIN_PREFIX}${MIN_API_LEVEL}-clang++"
export AR="$TOOLCHAIN_BIN/llvm-ar"
export RANLIB="$TOOLCHAIN_BIN/llvm-ranlib"

# Create symlinks for OpenSSL 3.x compatibility
SYMLINK_DIR="/tmp/openssl_android_toolchain"
rm -rf "$SYMLINK_DIR"
mkdir -p "$SYMLINK_DIR"

# Create wrapper scripts instead of symlinks for better compatibility
cat > "$SYMLINK_DIR/${TOOLCHAIN_PREFIX}-gcc" << EOF
#!/bin/bash
exec "$CC" "\$@"
EOF

cat > "$SYMLINK_DIR/${TOOLCHAIN_PREFIX}-g++" << EOF
#!/bin/bash  
exec "$CXX" "\$@"
EOF

cat > "$SYMLINK_DIR/${TOOLCHAIN_PREFIX}-ar" << EOF
#!/bin/bash
exec "$AR" "\$@"
EOF

cat > "$SYMLINK_DIR/${TOOLCHAIN_PREFIX}-ranlib" << EOF
#!/bin/bash
exec "$RANLIB" "\$@"
EOF

# Make wrapper scripts executable
chmod +x "$SYMLINK_DIR/${TOOLCHAIN_PREFIX}-gcc"
chmod +x "$SYMLINK_DIR/${TOOLCHAIN_PREFIX}-g++"
chmod +x "$SYMLINK_DIR/${TOOLCHAIN_PREFIX}-ar"
chmod +x "$SYMLINK_DIR/${TOOLCHAIN_PREFIX}-ranlib"

# Add symlink directory to PATH
export PATH="$SYMLINK_DIR:/usr/bin:/bin:$PATH"

echo "[DEBUG] OpenSSL environment:"
echo "[DEBUG] ANDROID_NDK_ROOT=$ANDROID_NDK_ROOT"
echo "[DEBUG] CC=$CC"
echo "[DEBUG] AR=$AR"
echo "[DEBUG] Symlink dir: $SYMLINK_DIR"
echo "[DEBUG] PATH (first part): $(echo $PATH | cut -d: -f1)"

# Verify symlinks work
echo "[DEBUG] Checking symlinks:"
ls -la "$SYMLINK_DIR/"

# Build OpenSSL
echo "[INFO] Building OpenSSL $OPENSSL_VERSION for $ABI..."
cd "$OPENSSL_SOURCE_DIR"

# Clean previous build
rm -f Makefile configdata.pm

# Configure with cross-compile prefix
echo "[INFO] Configuring OpenSSL..."

# Set environment for Perl to find our tools
export PATH="$SYMLINK_DIR:/usr/bin:/bin:$PATH"

# Debug PATH visibility
echo "[DEBUG] PATH for Configure: $(echo $PATH | cut -d: -f1-3)"
echo "[DEBUG] gcc test: $(which ${TOOLCHAIN_PREFIX}-gcc 2>/dev/null || echo 'NOT FOUND')"

perl Configure $OPENSSL_ARCH \
    CC="${TOOLCHAIN_PREFIX}-gcc" \
    CXX="${TOOLCHAIN_PREFIX}-g++" \
    AR="${TOOLCHAIN_PREFIX}-ar" \
    RANLIB="${TOOLCHAIN_PREFIX}-ranlib" \
    --prefix="$OPENSSL_INSTALL_DIR" \
    --openssldir="$OPENSSL_INSTALL_DIR" \
    no-shared \
    no-tests \
    no-ui-console \
    -D__ANDROID_API__=$MIN_API_LEVEL

if [ $? -ne 0 ]; then
    echo "[ERROR] OpenSSL Configure failed"
    exit 1
fi

# Build
echo "[INFO] Building OpenSSL..."
make -j$(nproc)

if [ $? -ne 0 ]; then
    echo "[ERROR] OpenSSL build failed"
    exit 1
fi

# Install
echo "[INFO] Installing OpenSSL..."
make install_sw

# Build libssh with OpenSSL
echo "[INFO] Building libssh $LIBSSH_VERSION with OpenSSL for $ABI..."

cd "$WORK_DIR"
LIBSSH_BUILD_DIR="$WORK_DIR/build/$ABI"
mkdir -p "$LIBSSH_BUILD_DIR"
cd "$LIBSSH_BUILD_DIR"

# Convert paths back to Windows format for CMake
ANDROID_NDK_HOME_WIN=$(echo "$ANDROID_NDK_HOME" | sed 's|^/\([a-z]\)/|\1:/|' | sed 's|/|\\|g')
OPENSSL_INSTALL_DIR_WIN=$(echo "$OPENSSL_INSTALL_DIR" | sed 's|^/\([a-z]\)/|\1:/|' | sed 's|/|\\|g')
LIBSSH_INSTALL_DIR_WIN=$(echo "$LIBSSH_INSTALL_DIR" | sed 's|^/\([a-z]\)/|\1:/|' | sed 's|/|\\|g')

echo "[DEBUG] CMake paths:"
echo "[DEBUG] NDK: $ANDROID_NDK_HOME_WIN"
echo "[DEBUG] OpenSSL: $OPENSSL_INSTALL_DIR_WIN"
echo "[DEBUG] libssh install: $LIBSSH_INSTALL_DIR_WIN"

# Configure libssh with CMake
cmake \
    -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK_HOME_WIN\\build\\cmake\\android.toolchain.cmake" \
    -DANDROID_ABI=$ABI \
    -DANDROID_PLATFORM=android-$MIN_API_LEVEL \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$LIBSSH_INSTALL_DIR_WIN" \
    -DBUILD_SHARED_LIBS=OFF \
    -DWITH_EXAMPLES=OFF \
    -DWITH_TESTING=OFF \
    -DWITH_SERVER=OFF \
    -DWITH_ZLIB=OFF \
    -DWITH_GSSAPI=OFF \
    -DWITH_PCAP=OFF \
    -DWITH_SFTP=ON \
    -DWITH_STATIC_LIB=ON \
    -DOPENSSL_ROOT_DIR="$OPENSSL_INSTALL_DIR_WIN" \
    -DOPENSSL_LIBRARIES="$OPENSSL_INSTALL_DIR_WIN\\lib\\libssl.a;$OPENSSL_INSTALL_DIR_WIN\\lib\\libcrypto.a" \
    -DOPENSSL_INCLUDE_DIR="$OPENSSL_INSTALL_DIR_WIN\\include" \
    "$LIBSSH_SOURCE_DIR"

if [ $? -ne 0 ]; then
    echo "[ERROR] libssh CMake configuration failed"
    exit 1
fi

# Build libssh
echo "[INFO] Building libssh..."
make -j$(nproc)

if [ $? -ne 0 ]; then
    echo "[ERROR] libssh build failed"
    exit 1
fi

# Install libssh
echo "[INFO] Installing libssh..."
make install

echo "[SUCCESS] OpenSSL $OPENSSL_VERSION and libssh $LIBSSH_VERSION built successfully for $ABI"
echo "[INFO] OpenSSL installed to: $OPENSSL_INSTALL_DIR"
echo "[INFO] libssh installed to: $LIBSSH_INSTALL_DIR"

# Display results
echo ""
echo "[INFO] Build results:"
echo "OpenSSL libraries:"
ls -la "$OPENSSL_INSTALL_DIR/lib/"*.a 2>/dev/null || echo "No OpenSSL .a files found"
echo ""
echo "libssh libraries:"
ls -la "$LIBSSH_INSTALL_DIR/lib/"*.a 2>/dev/null || echo "No libssh .a files found"
