#!/bin/bash

# Script to verify build in a clean CI environment
# Simulates conditions of GitHub Actions or other CI systems
# Usage: ./verify_ci_build.sh [clean|check] [ABI]

set -e

MODE=${1:-check}
ABI=${2:-x86}

echo "🧪 Testing build in CI environment..."
echo "Mode: $MODE, Architecture: $ABI"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

check_pass() {
    echo -e "${GREEN}✅ $1${NC}"
}

check_fail() {
    echo -e "${RED}❌ $1${NC}"
    exit 1
}

check_warn() {
    echo -e "${YELLOW}⚠️  $1${NC}"
}

info() {
    echo -e "${BLUE}ℹ️  $1${NC}"
}

if [ "$MODE" = "clean" ]; then
    # Clean CI test - clone repository fresh
    TEMP_DIR="/tmp/ci-test-$(date +%s)"
    REPO_URL="https://github.com/dobord/android-udp-bridge.git"
    BRANCH="openssl"

    # Check required environment variables
    if [ -z "$ANDROID_NDK_HOME" ]; then
    check_fail "ANDROID_NDK_HOME is not set"
    fi

    info "Creating temp directory: $TEMP_DIR"
    mkdir -p "$TEMP_DIR"
    cd "$TEMP_DIR"

    info "Cloning repository..."
    git clone "$REPO_URL" android-udp-bridge
    cd android-udp-bridge

    info "Switching to branch $BRANCH..."
    git checkout "$BRANCH"

    info "Building for $ABI..."
    export ANDROID_ABI="$ABI"
    ./build_openssl.sh

    # Verify result
    X86_SSL_LIB="ssh-tunnel-android-app/app/src/main/prebuilt/openssl/$ABI/lib/libssl.a"
    X86_SSH_LIB="ssh-tunnel-android-app/app/src/main/prebuilt/libssh/$ABI/lib/libssh.a"

    if [ -f "$X86_SSL_LIB" ] && [ -f "$X86_SSH_LIB" ]; then
    check_pass "Build for $ABI successful!"
        
    # Verify architecture via llvm-objdump
        if command -v "$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-objdump" &> /dev/null; then
            ARCH_CHECK=$($ANDROID_NDK_HOME/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-objdump -f "$X86_SSL_LIB" | head -3)
            case $ABI in
                "x86")
                    if echo "$ARCH_CHECK" | grep -q "elf32-i386"; then
                        check_pass "Architecture OK: i386"
                    else
                        check_fail "Incorrect architecture for x86"
                    fi
                    ;;
                "x86_64")
                    if echo "$ARCH_CHECK" | grep -q "elf64-x86-64"; then
                        check_pass "Architecture OK: x86_64"
                    else
                        check_fail "Incorrect architecture for x86_64"
                    fi
                    ;;
                "arm64-v8a")
                    if echo "$ARCH_CHECK" | grep -q "aarch64"; then
                        check_pass "Architecture OK: AArch64"
                    else
                        check_fail "Incorrect architecture for ARM64"
                    fi
                    ;;
                "armeabi-v7a")
                    if echo "$ARCH_CHECK" | grep -q "elf32-littlearm"; then
                        check_pass "Architecture OK: ARM32"
                    else
                        check_fail "Incorrect architecture for ARMv7"
                    fi
                    ;;
            esac
        else
            check_warn "llvm-objdump not found, skipping architecture validation"
        fi
        
    info "Library sizes:"
        ls -lh ssh-tunnel-android-app/app/src/main/prebuilt/openssl/$ABI/lib/*.a
        ls -lh ssh-tunnel-android-app/app/src/main/prebuilt/libssh/$ABI/lib/*.a
    else
    check_fail "Build failed - libraries not found"
    fi

    info "Cleaning temporary files..."
    cd /
    rm -rf "$TEMP_DIR"

    check_pass "CI test succeeded!"
    exit 0
fi

# "check" mode - validate existing build
echo ""
echo "📋 Verifying file structure for architecture: $ABI"

PREBUILT_DIR="$(pwd)/ssh-tunnel-android-app/app/src/main/prebuilt"
LIBSSH_DIR="$PREBUILT_DIR/libssh/$ABI"
OPENSSL_DIR="$PREBUILT_DIR/openssl/$ABI"

# Check libssh
if [ -f "$LIBSSH_DIR/lib/libssh.a" ]; then
    check_pass "libssh.a found"
    
    # Size check (should be > 500KB)
    SIZE=$(stat -c%s "$LIBSSH_DIR/lib/libssh.a")
    if [ $SIZE -gt 500000 ]; then
    check_pass "libssh.a has reasonable size ($(($SIZE/1024))KB)"
    else
    check_fail "libssh.a is too small ($(($SIZE/1024))KB)"
    fi
else
    check_fail "libssh.a not found in $LIBSSH_DIR/lib/"
fi

# Check libssh headers
REQUIRED_HEADERS=("libssh.h" "sftp.h" "callbacks.h")
for header in "${REQUIRED_HEADERS[@]}"; do
    if [ -f "$LIBSSH_DIR/include/libssh/$header" ]; then
    check_pass "Header $header found"
    else
    check_fail "Header $header not found"
    fi
done

# Check OpenSSL
OPENSSL_LIBS=("libssl.a" "libcrypto.a")
for lib in "${OPENSSL_LIBS[@]}"; do
    if [ -f "$OPENSSL_DIR/lib/$lib" ]; then
    check_pass "OpenSSL $lib found"
        
    # Size check
        SIZE=$(stat -c%s "$OPENSSL_DIR/lib/$lib")
        if [ $SIZE -gt 1000000 ]; then
            check_pass "OpenSSL $lib has reasonable size ($(($SIZE/1024/1024))MB)"
        else
            check_fail "OpenSSL $lib is too small ($(($SIZE/1024))KB)"
        fi
    else
    check_fail "OpenSSL $lib not found in $OPENSSL_DIR/lib/"
    fi
done

# Check OpenSSL headers
OPENSSL_HEADERS=("ssl.h" "crypto.h" "des.h" "aes.h")
for header in "${OPENSSL_HEADERS[@]}"; do
    if [ -f "$OPENSSL_DIR/include/openssl/$header" ]; then
    check_pass "OpenSSL header $header found"
    else
    check_fail "OpenSSL header $header not found"
    fi
done

echo ""
echo "🔍 Checking architecture..."

# Validate architecture by extracting an object file
TEMP_DIR=$(mktemp -d)
cd "$TEMP_DIR"

# Extract object file from libssh
ar -x "$LIBSSH_DIR/lib/libssh.a" 2>/dev/null
OBJ_FILE=$(ls *.o | head -1)

if [ -n "$OBJ_FILE" ]; then
    ARCH_INFO=$(readelf -h "$OBJ_FILE" 2>/dev/null | grep -E "(Machine|Class)" || true)
    
    case $ABI in
        "arm64-v8a")
            if echo "$ARCH_INFO" | grep -q "AArch64" && echo "$ARCH_INFO" | grep -q "ELF64"; then
                check_pass "Architecture ARM64 confirmed"
            else
                check_fail "Wrong architecture for ARM64: $ARCH_INFO"
            fi
            ;;
        "armeabi-v7a")
            if echo "$ARCH_INFO" | grep -q "ARM" && echo "$ARCH_INFO" | grep -q "ELF32"; then
                check_pass "Architecture ARMv7 confirmed"
            else
                check_fail "Wrong architecture for ARMv7: $ARCH_INFO"
            fi
            ;;
        "x86")
            if echo "$ARCH_INFO" | grep -q "Intel 80386" && echo "$ARCH_INFO" | grep -q "ELF32"; then
                check_pass "Architecture x86 confirmed"
            else
                check_fail "Wrong architecture for x86: $ARCH_INFO"
            fi
            ;;
        "x86_64")
            if echo "$ARCH_INFO" | grep -q "X86-64" && echo "$ARCH_INFO" | grep -q "ELF64"; then
                check_pass "Architecture x86_64 confirmed"
            else
                check_fail "Wrong architecture for x86_64: $ARCH_INFO"
            fi
            ;;
        *)
            check_warn "Unknown architecture $ABI, skipping validation"
            ;;
    esac
else
    check_fail "Failed to extract object file for architecture validation"
fi

# Cleanup
cd - > /dev/null
rm -rf "$TEMP_DIR"

echo ""
echo "🔍 Checking symbols..."

# Check main libssh symbols
LIBSSH_SYMBOLS=("ssh_connect" "ssh_new" "ssh_free")
for symbol in "${LIBSSH_SYMBOLS[@]}"; do
    if nm "$LIBSSH_DIR/lib/libssh.a" 2>/dev/null | grep -q "$symbol"; then
    check_pass "libssh symbol $symbol found"
    else
    check_fail "libssh symbol $symbol not found"
    fi
done

# Check main OpenSSL symbols
SSL_SYMBOLS=("SSL_connect" "SSL_new")
for symbol in "${SSL_SYMBOLS[@]}"; do
    if nm "$OPENSSL_DIR/lib/libssl.a" 2>/dev/null | grep -q "$symbol"; then
    check_pass "OpenSSL symbol $symbol found"
    else
    check_fail "OpenSSL symbol $symbol not found"
    fi
done

echo ""
echo "📊 Library size summary:"
echo "libssh: $(du -sh "$LIBSSH_DIR/lib/libssh.a" | cut -f1)"
echo "OpenSSL libssl: $(du -sh "$OPENSSL_DIR/lib/libssl.a" | cut -f1)"
echo "OpenSSL libcrypto: $(du -sh "$OPENSSL_DIR/lib/libcrypto.a" | cut -f1)"

echo ""
echo -e "${GREEN}🎉 Verification completed successfully for architecture $ABI${NC}"
