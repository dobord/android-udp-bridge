#!/bin/bash

# Wrapper for building libssh with OpenSSL 3.5 (mbedTLS path removed)

echo "🔄 OpenSSL 3.5 build wrapper..."

# Показываем переданную архитектуру
if [ -n "$ANDROID_ABI" ] && [ "$ANDROID_ABI" != "" ]; then
    echo "🎯 Целевая архитектура: $ANDROID_ABI"
else
    echo "🔄 Архитектура не задана, будут собраны все поддерживаемые"
fi

# Проверяем, есть ли новый скрипт
if [ -f "build_openssl.sh" ]; then
    echo "✅ Found build_openssl.sh"
    echo "🚀 Running libssh + OpenSSL build..."
    # Передаем все аргументы и переменные окружения
    exec bash ./build_openssl.sh "$@"
else
    echo "❌ Error: build_openssl.sh not found!"
    echo "📁 Ensure build_openssl.sh exists in project root"
    exit 1
fi
