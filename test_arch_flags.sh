#!/bin/bash

# Тестовый скрипт для проверки флагов компиляции для каждой архитектуры

set -e

echo "🧪 Тестирование флагов компиляции для Android архитектур"
echo "=========================================================="

# Проверяем переменные окружения
if [ -z "$ANDROID_NDK_HOME" ] && [ -z "$ANDROID_NDK_ROOT" ]; then
    if [ -d "$HOME/Android/Sdk/ndk" ]; then
        export ANDROID_NDK_HOME="$HOME/Android/Sdk/ndk/$(ls $HOME/Android/Sdk/ndk/ | sort -V | tail -1)"
    elif [ -d "/opt/android-sdk/ndk" ]; then
        export ANDROID_NDK_HOME="/opt/android-sdk/ndk/$(ls /opt/android-sdk/ndk/ | sort -V | tail -1)"
    else
        echo "❌ ANDROID_NDK_HOME не установлен и NDK не найден автоматически"
        exit 1
    fi
fi

NDK_PATH="${ANDROID_NDK_HOME:-$ANDROID_NDK_ROOT}"
echo "✅ Используем Android NDK: $NDK_PATH"

# Функция для получения флагов компиляции
get_arch_flags() {
    local ABI=$1
    local CFLAGS=""
    local LDFLAGS=""
    
    case $ABI in
        "arm64-v8a")
            CFLAGS="-march=armv8-a -fPIC"
            LDFLAGS="-Wl,-z,max-page-size=16384 -Wl,-z,common-page-size=16384"
            ;;
        "armeabi-v7a")
            CFLAGS="-march=armv7-a -mfloat-abi=softfp -mfpu=vfpv3-d16 -mthumb -fPIC"
            LDFLAGS="-Wl,--fix-cortex-a8"
            ;;
        "x86")
            CFLAGS="-march=i686 -msse3 -fPIC"
            LDFLAGS=""
            ;;
        "x86_64")
            CFLAGS="-march=x86-64 -msse4.2 -mpopcnt -m64 -fPIC"
            LDFLAGS=""
            ;;
        *)
            echo "❌ Неподдерживаемая архитектура: $ABI"
            return 1
            ;;
    esac
    
    echo "$CFLAGS|$LDFLAGS"
}

# Функция для тестирования архитектуры
test_abi() {
    local ABI=$1
    echo ""
    echo "🔍 Тестирование архитектуры: $ABI"
    echo "--------------------------------"
    
    # Получаем флаги
    local ARCH_FLAGS=$(get_arch_flags "$ABI")
    if [ $? -ne 0 ]; then
        echo "❌ Ошибка получения флагов для $ABI"
        return 1
    fi
    
    local ARCH_CFLAGS=$(echo "$ARCH_FLAGS" | cut -d'|' -f1)
    local ARCH_LDFLAGS=$(echo "$ARCH_FLAGS" | cut -d'|' -f2)
    
    echo "📝 Флаги компиляции:"
    echo "   CFLAGS: $ARCH_CFLAGS"
    echo "   LDFLAGS: $ARCH_LDFLAGS"
    
    # Настройка переменных для архитектуры
    case $ABI in
        "arm64-v8a")
            TOOLCHAIN_PREFIX="aarch64-linux-android"
            OPENSSL_ARCH="android-arm64"
            ;;
        "armeabi-v7a")
            TOOLCHAIN_PREFIX="armv7a-linux-androideabi"
            OPENSSL_ARCH="android-arm"
            ;;
        "x86")
            TOOLCHAIN_PREFIX="i686-linux-android"
            OPENSSL_ARCH="android-x86"
            ;;
        "x86_64")
            TOOLCHAIN_PREFIX="x86_64-linux-android"
            OPENSSL_ARCH="android-x86_64"
            ;;
    esac
    
    # Проверяем наличие компилятора
    local TOOLCHAIN="$NDK_PATH/toolchains/llvm/prebuilt/linux-x86_64"
    local COMPILER="$TOOLCHAIN/bin/${TOOLCHAIN_PREFIX}24-clang"
    
    if [ -f "$COMPILER" ]; then
        echo "✅ Компилятор найден: $COMPILER"
        
        # Проверяем, что компилятор работает
        if "$COMPILER" --version > /dev/null 2>&1; then
            echo "✅ Компилятор функционален"
            
            # Тестируем компиляцию с флагами
            local TEST_C_FILE="/tmp/test_${ABI}.c"
            echo 'int main() { return 0; }' > "$TEST_C_FILE"
            
            if "$COMPILER" $ARCH_CFLAGS -c "$TEST_C_FILE" -o "/tmp/test_${ABI}.o" 2>/dev/null; then
                echo "✅ Флаги компиляции работают корректно"
                rm -f "/tmp/test_${ABI}.o"
            else
                echo "❌ Ошибка компиляции с флагами: $ARCH_CFLAGS"
            fi
            
            rm -f "$TEST_C_FILE"
        else
            echo "❌ Компилятор не работает"
        fi
    else
        echo "❌ Компилятор не найден: $COMPILER"
        echo "📁 Доступные компиляторы:"
        ls -la "$TOOLCHAIN/bin/" | grep "${TOOLCHAIN_PREFIX%%-*}" | head -5
    fi
    
    echo "🎯 OpenSSL архитектура: $OPENSSL_ARCH"
}

# Тестируем все архитектуры
ABIS=("arm64-v8a" "armeabi-v7a" "x86_64" "x86")

for ABI in "${ABIS[@]}"; do
    test_abi "$ABI"
done

echo ""
echo "🎉 Тестирование флагов компиляции завершено!"
echo ""
echo "📋 Краткая сводка по архитектурам:"
echo "  - arm64-v8a: 16K страницы, ARMv8-A оптимизации"
echo "  - armeabi-v7a: Cortex-A8 fix, NEON, Thumb"
echo "  - x86: SSE3 оптимизации для совместимости"
echo "  - x86_64: SSE4.2, POPCNT, 64-bit оптимизации"
