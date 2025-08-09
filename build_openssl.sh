#!/bin/bash

# Скрипт для сборки OpenSSL 3.5 и libssh для Android
# Требует Android NDK и perl для сборки OpenSSL

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

# Проверяем наличие perl
if ! command -v perl &> /dev/null; then
    echo "Ошибка: perl не найден. Для сборки OpenSSL требуется perl."
    echo "Установите perl: sudo apt-get install perl"
    exit 1
fi

# Параметры сборки
LIBSSH_VERSION="0.11.2"
OPENSSL_VERSION="3.5.0"
MIN_API_LEVEL=24

# Определяем архитектуры для сборки
if [ -n "$ANDROID_ABI" ]; then
    # Если задана переменная ANDROID_ABI (например, в CI), используем только её
    ABIS=("$ANDROID_ABI")
    echo "🎯 Сборка для архитектуры из ANDROID_ABI: $ANDROID_ABI"
else
    # По умолчанию собираем все основные архитектуры
    ABIS=("arm64-v8a" "armeabi-v7a" "x86_64" "x86")
    echo "🔄 Сборка для всех архитектур: ${ABIS[*]}"
fi

# Рабочие директории
WORK_DIR="$(pwd)/openssl_build"
SOURCE_DIR="$WORK_DIR/libssh-$LIBSSH_VERSION"
BUILD_BASE_DIR="$WORK_DIR/build"
# Устанавливаем прямо в пребилды Android модуля
INSTALL_BASE_DIR="$(pwd)/ssh-tunnel-android-app/app/src/main/prebuilt"

# Директории для OpenSSL
OPENSSL_WORK_DIR="$WORK_DIR/openssl"
OPENSSL_SOURCE_DIR="$OPENSSL_WORK_DIR/openssl-$OPENSSL_VERSION"
OPENSSL_BUILD_BASE_DIR="$OPENSSL_WORK_DIR/build"

echo "Создаём рабочие директории..."
mkdir -p "$WORK_DIR"
mkdir -p "$INSTALL_BASE_DIR"

# Скачиваем OpenSSL, если ещё не скачан
if [ ! -f "$OPENSSL_WORK_DIR/openssl-$OPENSSL_VERSION.tar.gz" ]; then
    echo "Скачиваем OpenSSL $OPENSSL_VERSION..."
    mkdir -p "$OPENSSL_WORK_DIR"
    cd "$OPENSSL_WORK_DIR"
    wget -O "openssl-$OPENSSL_VERSION.tar.gz" "https://github.com/openssl/openssl/releases/download/openssl-$OPENSSL_VERSION/openssl-$OPENSSL_VERSION.tar.gz"
    tar -xf "openssl-$OPENSSL_VERSION.tar.gz"
fi

# Скачиваем libssh, если ещё не скачан
if [ ! -f "$WORK_DIR/libssh-$LIBSSH_VERSION.tar.xz" ]; then
    echo "Скачиваем libssh $LIBSSH_VERSION..."
    cd "$WORK_DIR"
    # Начиная с 0.11 используется каталог 0.11
    wget "https://www.libssh.org/files/0.11/libssh-$LIBSSH_VERSION.tar.xz"
    tar -xf "libssh-$LIBSSH_VERSION.tar.xz"
fi

# Функция для получения флагов компиляции для конкретной архитектуры
get_arch_flags() {
    local ABI=$1
    local CFLAGS=""
    local LDFLAGS=""
    
    case $ABI in
        "arm64-v8a")
            # Флаги для ARM64 - поддержка 16K страниц для Android 15+
            CFLAGS="-march=armv8-a -fPIC"
            LDFLAGS="-Wl,-z,max-page-size=16384 -Wl,-z,common-page-size=16384"
            ;;
        "armeabi-v7a")
            # Флаги для ARMv7 - совместимость с thumb и NEON (исправленные для Android)
            CFLAGS="-march=armv7-a -mfloat-abi=softfp -mfpu=neon -mthumb -fPIC"
            LDFLAGS="-Wl,--fix-cortex-a8 -Wl,-m,armelf_linux_eabi"
            ;;
        "x86")
            # Флаги для x86 - поддержка SSE и совместимость (упрощённые флаги)
            CFLAGS="-march=i686 -msse3 -fPIC"
            LDFLAGS=""
            ;;
        "x86_64")
            # Флаги для x86_64 - оптимизация для современных процессоров (исправленные для Android)
            CFLAGS="-march=x86-64 -msse4.2 -mpopcnt -m64 -fPIC -Wno-macro-redefined"
            LDFLAGS=""
            ;;
        *)
            echo "Неподдерживаемая архитектура: $ABI"
            return 1
            ;;
    esac
    
    echo "$CFLAGS|$LDFLAGS"
}

# Функция для сборки под конкретную архитектуру
build_for_abi() {
    local ABI=$1
    echo "Сборка OpenSSL и libssh для архитектуры: $ABI"
    
    # Получаем флаги для архитектуры
    local ARCH_FLAGS=$(get_arch_flags "$ABI")
    if [ $? -ne 0 ]; then
        echo "Ошибка получения флагов для архитектуры $ABI"
        return 1
    fi
    
    local ARCH_CFLAGS=$(echo "$ARCH_FLAGS" | cut -d'|' -f1)
    local ARCH_LDFLAGS=$(echo "$ARCH_FLAGS" | cut -d'|' -f2)
    
    echo "Флаги для архитектуры $ABI:"
    echo "  CFLAGS: $ARCH_CFLAGS"
    echo "  LDFLAGS: $ARCH_LDFLAGS"
    
    # Настройка переменных для архитектуры
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
            echo "Неподдерживаемая архитектура: $ABI"
            return 1
            ;;
    esac
    
    BUILD_DIR="$BUILD_BASE_DIR/$ABI"
    INSTALL_DIR="$INSTALL_BASE_DIR/libssh/$ABI"
    OPENSSL_BUILD_DIR="$OPENSSL_BUILD_BASE_DIR/$ABI"
    OPENSSL_INSTALL_DIR="$INSTALL_BASE_DIR/openssl/$ABI"

    # Настройка путей для Android toolchain
    TOOLCHAIN="$NDK_PATH/toolchains/llvm/prebuilt/linux-x86_64"
    
    # Проверим, что toolchain существует
    if [ ! -d "$TOOLCHAIN" ]; then
        echo "Ошибка: Android toolchain не найден в $TOOLCHAIN"
        exit 1
    fi
    
    # Определяем полные флаги компиляции с учетом архитектуры
    FULL_CFLAGS="-DS_IWRITE=S_IWUSR $ARCH_CFLAGS"
    FULL_LDFLAGS="$ARCH_LDFLAGS"
    
    export AR="$TOOLCHAIN/bin/llvm-ar"
    export CC="$TOOLCHAIN/bin/${TOOLCHAIN_PREFIX}${MIN_API_LEVEL}-clang"
    export CXX="$TOOLCHAIN/bin/${TOOLCHAIN_PREFIX}${MIN_API_LEVEL}-clang++"
    export ASM="$CC"
    export STRIP="$TOOLCHAIN/bin/llvm-strip"
    export RANLIB="$TOOLCHAIN/bin/llvm-ranlib"
    export PATH="$TOOLCHAIN/bin:$PATH"
    
    # Экспортируем флаги компиляции
    export CFLAGS="$FULL_CFLAGS"
    export CXXFLAGS="$FULL_CFLAGS"
    export LDFLAGS="$FULL_LDFLAGS"
    
    # Дополнительные переменные для OpenSSL
    export ANDROID_NDK_ROOT="$NDK_PATH"
    export CROSS_COMPILE="${TOOLCHAIN_PREFIX}${MIN_API_LEVEL}-"
    
    echo "Настроены переменные окружения для $ABI:"
    echo "  CC: $CC"
    echo "  CFLAGS: $CFLAGS"
    echo "  LDFLAGS: $LDFLAGS"
    echo "  Toolchain: $TOOLCHAIN"

    # Чистим каталоги сборки для предотвращения конфликтов
    rm -rf "$BUILD_DIR" "$OPENSSL_BUILD_DIR"
    mkdir -p "$BUILD_DIR" "$INSTALL_DIR" "$OPENSSL_BUILD_DIR" "$OPENSSL_INSTALL_DIR"

    # 1) Сборка OpenSSL (статические библиотеки)
    echo "Сборка OpenSSL для архитектуры: $ABI"
    
    # Копируем исходники OpenSSL в build директорию для изоляции
    OPENSSL_BUILD_SOURCE="$OPENSSL_BUILD_DIR/openssl-$OPENSSL_VERSION"
    rm -rf "$OPENSSL_BUILD_SOURCE"
    cp -r "$OPENSSL_SOURCE_DIR" "$OPENSSL_BUILD_SOURCE"
    cd "$OPENSSL_BUILD_SOURCE"
    
    # Очищаем предыдущую конфигурацию
    if [ -f Makefile ]; then
        make clean || true
    fi
    
    # Конфигурируем OpenSSL для Android
    echo "Конфигурируем OpenSSL с параметрами:"
    echo "  Target: $OPENSSL_ARCH"
    echo "  API Level: $MIN_API_LEVEL"
    echo "  Install Dir: $OPENSSL_INSTALL_DIR"
    
    # Специальные настройки для ARMv7 и x86_64 чтобы избежать проблем линковки
    if [ "$ABI" = "armeabi-v7a" ] || [ "$ABI" = "x86_64" ]; then
        echo "Применяем специальные настройки для $ABI..."
        ./Configure $OPENSSL_ARCH \
            -D__ANDROID_API__=$MIN_API_LEVEL \
            --prefix="$OPENSSL_INSTALL_DIR" \
            --openssldir="$OPENSSL_INSTALL_DIR" \
            no-shared \
            no-tests \
            no-ui-console \
            no-docs \
            no-apps \
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
    
    # Проверяем, что Configure прошел успешно
    if [ ! -f Makefile ]; then
        echo "Ошибка: OpenSSL Configure не создал Makefile"
        echo "Проверьте логи выше для деталей ошибки"
        exit 1
    fi
    
    # Собираем и устанавливаем
    echo "Компилируем OpenSSL..."
    if [ "$ABI" = "armeabi-v7a" ] || [ "$ABI" = "x86_64" ]; then
        # Для ARMv7 и x86_64 собираем только библиотеки без приложений
        echo "Сборка только библиотек для $ABI..."
        make -j$(nproc) build_libs
    else
        make -j$(nproc)
    fi
    
    echo "Устанавливаем OpenSSL..."
    if [ "$ABI" = "armeabi-v7a" ] || [ "$ABI" = "x86_64" ]; then
        # Для ARMv7 и x86_64 устанавливаем только библиотеки
        make install_dev
    else
        make install_sw
    fi
    
    # Проверяем результат
    if [ ! -f "$OPENSSL_INSTALL_DIR/lib/libssl.a" ] || [ ! -f "$OPENSSL_INSTALL_DIR/lib/libcrypto.a" ]; then
        echo "Ошибка: OpenSSL библиотеки не найдены после установки"
        echo "Ожидались: $OPENSSL_INSTALL_DIR/lib/libssl.a и libcrypto.a"
        exit 1
    fi
    
    echo "✅ OpenSSL для $ABI установлен в $OPENSSL_INSTALL_DIR"

    # 2) Сборка libssh со связкой на OpenSSL
    echo "Сборка libssh для архитектуры: $ABI"
    cd "$BUILD_DIR"
    
    # Проверяем, что OpenSSL библиотеки доступны
    if [ ! -f "$OPENSSL_INSTALL_DIR/lib/libssl.a" ] || [ ! -f "$OPENSSL_INSTALL_DIR/lib/libcrypto.a" ]; then
        echo "Ошибка: OpenSSL библиотеки не найдены для libssh сборки"
        exit 1
    fi
    
    echo "Конфигурируем libssh с параметрами:"
    echo "  OpenSSL Root: $OPENSSL_INSTALL_DIR"
    echo "  Install Dir: $INSTALL_DIR"
    echo "  ABI: $ANDROID_ABI"
    
    # Конфигурация CMake для Android
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
    
    # Проверяем, что CMake конфигурация прошла успешно
    if [ $? -ne 0 ]; then
        echo "Ошибка: CMake конфигурация libssh не удалась"
        exit 1
    fi
    
    # Сборка и установка
    echo "Компилируем libssh..."
    cmake --build . --config Release -- -j$(nproc)
    
    if [ $? -ne 0 ]; then
        echo "Ошибка: Компиляция libssh не удалась"
        exit 1
    fi
    
    echo "Устанавливаем libssh..."
    cmake --install .
    
    # Проверяем результат
    if [ ! -f "$INSTALL_DIR/lib/libssh.a" ]; then
        echo "Ошибка: libssh библиотека не найдена после установки"
        echo "Ожидалась: $INSTALL_DIR/lib/libssh.a"
        exit 1
    fi
    
    echo "✅ Сборка для $ABI завершена"
}

# Сборка для всех архитектур
for ABI in "${ABIS[@]}"; do
    build_for_abi "$ABI"
done

echo ""
echo "🎉 Сборка libssh с OpenSSL для всех архитектур завершена!"
echo ""
echo "Библиотеки установлены в:"
echo "  - libssh: $INSTALL_BASE_DIR/libssh/"
echo "  - OpenSSL: $INSTALL_BASE_DIR/openssl/"
echo ""
echo "Структура файлов:"
find "$INSTALL_BASE_DIR/libssh" -name "*.a" -o -name "*.so" | head -10
echo ""
find "$INSTALL_BASE_DIR/openssl" -name "*.a" -o -name "*.so" | head -10

echo ""
echo "📁 Для интеграции обновите CMakeLists.txt чтобы использовать:"
echo "   - Заголовочные файлы libssh: \$INSTALL_DIR/libssh/\$ABI/include"
echo "   - Библиотека libssh: \$INSTALL_DIR/libssh/\$ABI/lib/libssh.a"
echo "   - Заголовочные файлы OpenSSL: \$INSTALL_DIR/openssl/\$ABI/include"
echo "   - Библиотеки OpenSSL: \$INSTALL_DIR/openssl/\$ABI/lib/libssl.a и libcrypto.a"
