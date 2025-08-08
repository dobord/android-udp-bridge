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
LIBSSH_VERSION="0.10.6"
MIN_API_LEVEL=21
ABIS=("arm64-v8a" "armeabi-v7a" "x86" "x86_64")

# Рабочие директории
WORK_DIR="$(pwd)/libssh_build"
SOURCE_DIR="$WORK_DIR/libssh-$LIBSSH_VERSION"
BUILD_BASE_DIR="$WORK_DIR/build"
INSTALL_BASE_DIR="$(pwd)/app/src/main/prebuilt"

echo "Создаём рабочие директории..."
mkdir -p "$WORK_DIR"
mkdir -p "$INSTALL_BASE_DIR"

# Скачиваем libssh, если ещё не скачан
if [ ! -f "$WORK_DIR/libssh-$LIBSSH_VERSION.tar.xz" ]; then
    echo "Скачиваем libssh $LIBSSH_VERSION..."
    cd "$WORK_DIR"
    wget "https://www.libssh.org/files/0.10/libssh-$LIBSSH_VERSION.tar.xz"
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
    
    mkdir -p "$BUILD_DIR"
    mkdir -p "$INSTALL_DIR"
    
    cd "$BUILD_DIR"
    
    # Конфигурация CMake для Android
    cmake "$SOURCE_DIR" \
        -DCMAKE_SYSTEM_NAME=Android \
        -DCMAKE_ANDROID_ARCH_ABI=$ANDROID_ABI \
        -DCMAKE_ANDROID_NDK="$NDK_PATH" \
        -DCMAKE_ANDROID_API=$MIN_API_LEVEL \
        -DCMAKE_ANDROID_STL_TYPE=c++_shared \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR" \
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
        -DWITH_EXAMPLES=OFF \
        -DWITH_TESTING=OFF \
        -DWITH_SERVER=OFF \
        -DWITH_ZLIB=OFF \
        -DWITH_GSSAPI=OFF \
        -DWITH_PCAP=OFF \
        -DWITH_SFTP=ON \
        -DBUILD_SHARED_LIBS=OFF
    
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
