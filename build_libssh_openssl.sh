#!/bin/bash

# Wrapper для сборки с OpenSSL 3.5
# Этот скрипт заменяет build_libssh.sh для совместимости

echo "🔄 Переключение на сборку с OpenSSL 3.5..."
echo ""

# Проверяем, есть ли новый скрипт
if [ -f "build_openssl.sh" ]; then
    echo "✅ Найден скрипт сборки OpenSSL: build_openssl.sh"
    echo "🚀 Запускаем сборку libssh с OpenSSL 3.5..."
    exec bash ./build_openssl.sh "$@"
else
    echo "❌ Ошибка: build_openssl.sh не найден!"
    echo "📁 Убедитесь, что файл build_openssl.sh существует в корне проекта"
    exit 1
fi
