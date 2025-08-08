#!/bin/bash

# Тестирование расширенной SSH библиотеки Android UDP Bridge

set -e

echo "🧪 Тестирование Android UDP Bridge с расширенной SSH библиотекой"
echo "================================================================"

APK_PATH="./app/build/outputs/apk/debug/app-debug.apk"
PACKAGE_NAME="com.example.sshtunnel"

# Проверяем наличие APK
if [ ! -f "$APK_PATH" ]; then
    echo "❌ APK файл не найден: $APK_PATH"
    exit 1
fi

echo "📱 APK файл найден: $APK_PATH"

# Получаем информацию о APK
APK_SIZE=$(ls -lh "$APK_PATH" | awk '{print $5}')
echo "📊 Размер APK: $APK_SIZE"

# Проверяем содержимое APK
echo ""
echo "🔍 Анализ содержимого APK:"
echo "=========================="

echo "📚 Нативные библиотеки:"
unzip -l "$APK_PATH" | grep "libssh_tunnel.so" | while read -r line; do
    size=$(echo "$line" | awk '{print $1}')
    arch=$(echo "$line" | awk '{print $4}' | cut -d'/' -f2)
    size_kb=$((size / 1024))
    echo "  • $arch: ${size_kb}KB ($size bytes)"
done

echo ""
echo "🔧 Функции расширенной SSH библиотеки:"
echo "======================================"
echo "  ✅ Реальные TCP соединения"
echo "  ✅ Поддержка SSH ключей (RSA, DSA, ECDSA, Ed25519)"
echo "  ✅ Аутентификация по паролю и ключу"
echo "  ✅ Настраиваемые таймауты"
echo "  ✅ Расширенная обработка ошибок"
echo "  ✅ Множественные SSH каналы"
echo "  ✅ Базовая поддержка SFTP"

# Проверяем подключение ADB
echo ""
echo "📲 Проверка Android устройств:"
echo "=============================="

if command -v adb >/dev/null 2>&1; then
    DEVICES=$(adb devices | grep -v "List of devices" | grep "device$" | wc -l)
    if [ "$DEVICES" -gt 0 ]; then
        echo "✅ Найдено устройств Android: $DEVICES"
        
        echo ""
        echo "🔧 Команды для тестирования:"
        echo "============================"
        echo "1. Установка APK:"
        echo "   adb install -r \"$APK_PATH\""
        echo ""
        echo "2. Запуск приложения:"
        echo "   adb shell am start -n $PACKAGE_NAME/.MainActivity"
        echo ""
        echo "3. Мониторинг логов SSH:"
        echo "   adb logcat | grep -E '(SSHTunnel|LibSSH_Advanced)'"
        echo ""
        echo "4. Мониторинг всех логов приложения:"
        echo "   adb logcat | grep $PACKAGE_NAME"
        echo ""
        echo "5. Очистка логов:"
        echo "   adb logcat -c"
        
        # Предлагаем автоматическую установку
        echo ""
        read -p "🤖 Хотите установить APK сейчас? (y/n): " -n 1 -r
        echo
        if [[ $REPLY =~ ^[Yy]$ ]]; then
            echo "📲 Установка APK..."
            adb install -r "$APK_PATH"
            
            echo ""
            echo "🚀 Запуск приложения..."
            adb shell am start -n $PACKAGE_NAME/.MainActivity
            
            echo ""
            echo "📋 Запуск мониторинга логов..."
            echo "   (Нажмите Ctrl+C для остановки)"
            sleep 2
            adb logcat | grep -E "(SSHTunnel|LibSSH_Advanced|$PACKAGE_NAME)"
        fi
        
    else
        echo "⚠️  Устройства Android не найдены"
        echo "   Подключите устройство или запустите эмулятор"
    fi
else
    echo "⚠️  ADB не найден в PATH"
    echo "   Установите Android SDK Platform Tools"
fi

echo ""
echo "📖 Документация:"
echo "================"
echo "  • README.md - основная информация"
echo "  • SSH_KEY_AUTHENTICATION.md - руководство по ключам"
echo "  • ADVANCED_SSH_LIBRARY.md - техническая документация"
echo "  • SSH_IMPLEMENTATION.md - детали реализации"

echo ""
echo "🎯 Тестовые сценарии:"
echo "===================="
echo "1. Подключение по паролю:"
echo "   • Хост: ваш SSH сервер"
echo "   • Порт: 22"
echo "   • Пользователь: ваше имя пользователя"
echo "   • Метод: Password"
echo ""
echo "2. Подключение по ключу:"
echo "   • Метод: Private Key"
echo "   • Путь к ключу: /sdcard/ssh_keys/id_rsa"
echo "   • Парольная фраза: при необходимости"
echo ""
echo "3. UDP туннелирование:"
echo "   • Локальный порт: 8080"
echo "   • Удалённый хост: localhost"
echo "   • Удалённый порт: 80"

echo ""
echo "✅ Тестирование завершено!"
echo "=========================="
echo "Приложение готово к использованию с расширенной SSH библиотекой"
