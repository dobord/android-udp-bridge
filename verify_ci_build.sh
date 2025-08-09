#!/bin/bash

# Скрипт для проверки сборки в чистом CI окружении
# Симулирует условия GitHub Actions или других CI систем
# Использование: ./verify_ci_build.sh [clean|check] [ABI]

set -e

MODE=${1:-check}
ABI=${2:-x86}

echo "🧪 Тестирование сборки в CI окружении..."
echo "Режим: $MODE, Архитектура: $ABI"

# Цвета для вывода
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
    # Чистый CI тест - клонируем репозиторий заново
    TEMP_DIR="/tmp/ci-test-$(date +%s)"
    REPO_URL="https://github.com/dobord/android-udp-bridge.git"
    BRANCH="openssl"

    # Проверяем переменные окружения
    if [ -z "$ANDROID_NDK_HOME" ]; then
        check_fail "ANDROID_NDK_HOME не установлен"
    fi

    info "Создаём временную директорию: $TEMP_DIR"
    mkdir -p "$TEMP_DIR"
    cd "$TEMP_DIR"

    info "Клонируем репозиторий..."
    git clone "$REPO_URL" android-udp-bridge
    cd android-udp-bridge

    info "Переключаемся на ветку $BRANCH..."
    git checkout "$BRANCH"

    info "Тестируем сборку для $ABI..."
    export ANDROID_ABI="$ABI"
    ./build_openssl.sh

    # Проверяем результат
    X86_SSL_LIB="ssh-tunnel-android-app/app/src/main/prebuilt/openssl/$ABI/lib/libssl.a"
    X86_SSH_LIB="ssh-tunnel-android-app/app/src/main/prebuilt/libssh/$ABI/lib/libssh.a"

    if [ -f "$X86_SSL_LIB" ] && [ -f "$X86_SSH_LIB" ]; then
        check_pass "Сборка для $ABI успешна!"
        
        # Проверяем архитектуру
        if command -v "$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-objdump" &> /dev/null; then
            ARCH_CHECK=$($ANDROID_NDK_HOME/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-objdump -f "$X86_SSL_LIB" | head -3)
            case $ABI in
                "x86")
                    if echo "$ARCH_CHECK" | grep -q "elf32-i386"; then
                        check_pass "Архитектура корректна: i386"
                    else
                        check_fail "Неправильная архитектура для x86"
                    fi
                    ;;
                "x86_64")
                    if echo "$ARCH_CHECK" | grep -q "elf64-x86-64"; then
                        check_pass "Архитектура корректна: x86_64"
                    else
                        check_fail "Неправильная архитектура для x86_64"
                    fi
                    ;;
                "arm64-v8a")
                    if echo "$ARCH_CHECK" | grep -q "aarch64"; then
                        check_pass "Архитектура корректна: AArch64"
                    else
                        check_fail "Неправильная архитектура для ARM64"
                    fi
                    ;;
                "armeabi-v7a")
                    if echo "$ARCH_CHECK" | grep -q "elf32-littlearm"; then
                        check_pass "Архитектура корректна: ARM32"
                    else
                        check_fail "Неправильная архитектура для ARMv7"
                    fi
                    ;;
            esac
        else
            check_warn "llvm-objdump не найден, пропускаем проверку архитектуры"
        fi
        
        info "Размеры библиотек:"
        ls -lh ssh-tunnel-android-app/app/src/main/prebuilt/openssl/$ABI/lib/*.a
        ls -lh ssh-tunnel-android-app/app/src/main/prebuilt/libssh/$ABI/lib/*.a
    else
        check_fail "Сборка не удалась - библиотеки не найдены"
    fi

    info "Очищаем временные файлы..."
    cd /
    rm -rf "$TEMP_DIR"

    check_pass "CI тест прошёл успешно!"
    exit 0
fi

# Режим "check" - проверяем существующую сборку
echo ""
echo "📋 Проверка структуры файлов для архитектуры: $ABI"

PREBUILT_DIR="$(pwd)/ssh-tunnel-android-app/app/src/main/prebuilt"
LIBSSH_DIR="$PREBUILT_DIR/libssh/$ABI"
OPENSSL_DIR="$PREBUILT_DIR/openssl/$ABI"

# Проверка libssh
if [ -f "$LIBSSH_DIR/lib/libssh.a" ]; then
    check_pass "libssh.a найден"
    
    # Проверка размера (должен быть больше 500KB)
    SIZE=$(stat -c%s "$LIBSSH_DIR/lib/libssh.a")
    if [ $SIZE -gt 500000 ]; then
        check_pass "libssh.a имеет разумный размер ($(($SIZE/1024))KB)"
    else
        check_fail "libssh.a слишком мал ($(($SIZE/1024))KB)"
    fi
else
    check_fail "libssh.a не найден в $LIBSSH_DIR/lib/"
fi

# Проверка заголовочных файлов libssh
REQUIRED_HEADERS=("libssh.h" "sftp.h" "callbacks.h")
for header in "${REQUIRED_HEADERS[@]}"; do
    if [ -f "$LIBSSH_DIR/include/libssh/$header" ]; then
        check_pass "Заголовочный файл $header найден"
    else
        check_fail "Заголовочный файл $header не найден"
    fi
done

# Проверка OpenSSL
OPENSSL_LIBS=("libssl.a" "libcrypto.a")
for lib in "${OPENSSL_LIBS[@]}"; do
    if [ -f "$OPENSSL_DIR/lib/$lib" ]; then
        check_pass "OpenSSL $lib найден"
        
        # Проверка размера
        SIZE=$(stat -c%s "$OPENSSL_DIR/lib/$lib")
        if [ $SIZE -gt 1000000 ]; then
            check_pass "OpenSSL $lib имеет разумный размер ($(($SIZE/1024/1024))MB)"
        else
            check_fail "OpenSSL $lib слишком мал ($(($SIZE/1024))KB)"
        fi
    else
        check_fail "OpenSSL $lib не найден в $OPENSSL_DIR/lib/"
    fi
done

# Проверка заголовочных файлов OpenSSL
OPENSSL_HEADERS=("ssl.h" "crypto.h" "des.h" "aes.h")
for header in "${OPENSSL_HEADERS[@]}"; do
    if [ -f "$OPENSSL_DIR/include/openssl/$header" ]; then
        check_pass "OpenSSL заголовочный файл $header найден"
    else
        check_fail "OpenSSL заголовочный файл $header не найден"
    fi
done

echo ""
echo "🔍 Проверка архитектуры..."

# Проверка архитектуры через извлечение объектного файла
TEMP_DIR=$(mktemp -d)
cd "$TEMP_DIR"

# Извлекаем объектный файл из libssh
ar -x "$LIBSSH_DIR/lib/libssh.a" 2>/dev/null
OBJ_FILE=$(ls *.o | head -1)

if [ -n "$OBJ_FILE" ]; then
    ARCH_INFO=$(readelf -h "$OBJ_FILE" 2>/dev/null | grep -E "(Machine|Class)" || true)
    
    case $ABI in
        "arm64-v8a")
            if echo "$ARCH_INFO" | grep -q "AArch64" && echo "$ARCH_INFO" | grep -q "ELF64"; then
                check_pass "Архитектура ARM64 подтверждена"
            else
                check_fail "Неверная архитектура для ARM64: $ARCH_INFO"
            fi
            ;;
        "armeabi-v7a")
            if echo "$ARCH_INFO" | grep -q "ARM" && echo "$ARCH_INFO" | grep -q "ELF32"; then
                check_pass "Архитектура ARMv7 подтверждена"
            else
                check_fail "Неверная архитектура для ARMv7: $ARCH_INFO"
            fi
            ;;
        "x86")
            if echo "$ARCH_INFO" | grep -q "Intel 80386" && echo "$ARCH_INFO" | grep -q "ELF32"; then
                check_pass "Архитектура x86 подтверждена"
            else
                check_fail "Неверная архитектура для x86: $ARCH_INFO"
            fi
            ;;
        "x86_64")
            if echo "$ARCH_INFO" | grep -q "X86-64" && echo "$ARCH_INFO" | grep -q "ELF64"; then
                check_pass "Архитектура x86_64 подтверждена"
            else
                check_fail "Неверная архитектура для x86_64: $ARCH_INFO"
            fi
            ;;
        *)
            check_warn "Неизвестная архитектура $ABI, пропускаем проверку"
            ;;
    esac
else
    check_fail "Не удалось извлечь объектный файл для проверки архитектуры"
fi

# Очистка
cd - > /dev/null
rm -rf "$TEMP_DIR"

echo ""
echo "🔍 Проверка символов..."

# Проверка основных символов libssh
LIBSSH_SYMBOLS=("ssh_connect" "ssh_new" "ssh_free")
for symbol in "${LIBSSH_SYMBOLS[@]}"; do
    if nm "$LIBSSH_DIR/lib/libssh.a" 2>/dev/null | grep -q "$symbol"; then
        check_pass "Символ libssh $symbol найден"
    else
        check_fail "Символ libssh $symbol не найден"
    fi
done

# Проверка основных символов OpenSSL
SSL_SYMBOLS=("SSL_connect" "SSL_new")
for symbol in "${SSL_SYMBOLS[@]}"; do
    if nm "$OPENSSL_DIR/lib/libssl.a" 2>/dev/null | grep -q "$symbol"; then
        check_pass "Символ OpenSSL $symbol найден"
    else
        check_fail "Символ OpenSSL $symbol не найден"
    fi
done

echo ""
echo "📊 Сводка размеров библиотек:"
echo "libssh: $(du -sh "$LIBSSH_DIR/lib/libssh.a" | cut -f1)"
echo "OpenSSL libssl: $(du -sh "$OPENSSL_DIR/lib/libssl.a" | cut -f1)"
echo "OpenSSL libcrypto: $(du -sh "$OPENSSL_DIR/lib/libcrypto.a" | cut -f1)"

echo ""
echo -e "${GREEN}🎉 Проверка завершена успешно для архитектуры $ABI${NC}"
