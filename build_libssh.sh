#!/bin/bash

# Скрипт для сборки libssh для Android
# Требует Android NDK и CMake

set -e

# Проверяем переменные окружения
if [ -z "$ANDROID_NDK_HOME" ] && [ -z "$ANDROID_NDK_ROOT" ]; then
    # Пытаемся найти NDK автоматически
    if [ -d "$HOME/Android/Sdk/ndk" ]; then
        export ANDROID_NDK_HOME="$HOME/Android/Sdk/ndk/$(ls $HOME/Android/Sdk/ndk/ | sort -V | tail -1)"
    elif [ -d "/opt/android-sdk/ndk" ]; then
        export ANDROID_NDK_HOME="/opt/android-sdk/ndk/$(ls /opt/android-sdk/ndk/ | sort -V | tail -1)"
    else
        echo "Ошибка: ANDROID_NDK_HOME не установлен и NDK не найден автоматически"
        exit 1
    fi
fi

NDK_PATH="${ANDROID_NDK_HOME:-$ANDROID_NDK_ROOT}"
echo "Используем Android NDK: $NDK_PATH"

# Параметры сборки
LIBSSH_VERSION="0.11.2"
MBEDTLS_VERSION="2.28.7"
MIN_API_LEVEL=24
# Флаги для 16K страниц (Android 15)
MAX_PAGE_LDFLAGS="-Wl,-z,max-page-size=16384 -Wl,-z,common-page-size=16384"
# Ограничиваемся arm64-v8a для ускорения и соответствия настройкам приложения
ABIS=("arm64-v8a")

# Рабочие директории
WORK_DIR="$(pwd)/libssh_build"
SOURCE_DIR="$WORK_DIR/libssh-$LIBSSH_VERSION"
BUILD_BASE_DIR="$WORK_DIR/build"
# Устанавливаем прямо в пребиилды Android модуля
INSTALL_BASE_DIR="$(pwd)/ssh-tunnel-android-app/app/src/main/prebuilt"

# Директории для mbedTLS
MBEDTLS_WORK_DIR="$WORK_DIR/mbedtls"
MBEDTLS_SOURCE_DIR="$MBEDTLS_WORK_DIR/mbedtls-$MBEDTLS_VERSION"
MBEDTLS_BUILD_BASE_DIR="$MBEDTLS_WORK_DIR/build"

echo "Создаём рабочие директории..."
mkdir -p "$WORK_DIR"
mkdir -p "$INSTALL_BASE_DIR"

# Скачиваем mbedTLS, если ещё не скачан
if [ ! -f "$MBEDTLS_WORK_DIR/mbedtls-$MBEDTLS_VERSION.tar.gz" ]; then
    echo "Скачиваем mbedTLS $MBEDTLS_VERSION..."
    mkdir -p "$MBEDTLS_WORK_DIR"
    cd "$MBEDTLS_WORK_DIR"
    wget -O "mbedtls-$MBEDTLS_VERSION.tar.gz" "https://github.com/Mbed-TLS/mbedtls/archive/refs/tags/v$MBEDTLS_VERSION.tar.gz"
    tar -xf "mbedtls-$MBEDTLS_VERSION.tar.gz"
fi

# Скачиваем libssh, если ещё не скачан
if [ ! -f "$WORK_DIR/libssh-$LIBSSH_VERSION.tar.xz" ]; then
    echo "Скачиваем libssh $LIBSSH_VERSION..."
    cd "$WORK_DIR"
    # Начиная с 0.11 используется каталог 0.11
    wget "https://www.libssh.org/files/0.11/libssh-$LIBSSH_VERSION.tar.xz"
    tar -xf "libssh-$LIBSSH_VERSION.tar.xz"
fi

# Функция для сборки под конкретную архитектуру
build_for_abi() {
    local ABI=$1
    echo "Сборка libssh для архитектуры: $ABI"
    
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
            echo "Неподдерживаемая архитектура: $ABI"
            return 1
            ;;
    esac
    
    BUILD_DIR="$BUILD_BASE_DIR/$ABI"
    INSTALL_DIR="$INSTALL_BASE_DIR/libssh/$ABI"
    MBEDTLS_BUILD_DIR="$MBEDTLS_BUILD_BASE_DIR/$ABI"
    MBEDTLS_INSTALL_DIR="$INSTALL_BASE_DIR/mbedtls/$ABI"

    # Чистим каталоги сборки для предотвращения конфликтов CMakeCache
    rm -rf "$BUILD_DIR" "$MBEDTLS_BUILD_DIR"
    mkdir -p "$BUILD_DIR" "$INSTALL_DIR" "$MBEDTLS_BUILD_DIR" "$MBEDTLS_INSTALL_DIR"

    # 1) Сборка mbedTLS (статические библиотеки)
    echo "Сборка mbedTLS для архитектуры: $ABI"
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
    echo "✅ mbedTLS для $ABI установлена в $MBEDTLS_INSTALL_DIR"

    # 2) Сборка libssh со связкой на mbedTLS
    cd "$BUILD_DIR"
    
    # Конфигурация CMake для Android
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
    
    # Сборка и установка
    cmake --build . --config Release -- -j$(nproc)
    cmake --install .
    
    echo "✅ Сборка для $ABI завершена"
}

# Сборка для всех архитектур
for ABI in "${ABIS[@]}"; do
    build_for_abi "$ABI"
done

echo ""
echo "🎉 Сборка libssh для всех архитектур завершена!"
echo ""
echo "Библиотеки установлены в: $INSTALL_BASE_DIR/libssh/"
echo ""
echo "Структура файлов:"
find "$INSTALL_BASE_DIR/libssh" -name "*.a" -o -name "*.so" | head -20

echo ""
echo "📁 Для интеграции обновите CMakeLists.txt чтобы использовать:"
echo "   - Заголовочные файлы: \$INSTALL_DIR/include"
echo "   - Библиотеки: \$INSTALL_DIR/lib/libssh.a"
