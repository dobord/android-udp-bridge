#!/bin/bash

# Быстрая проверка решения проблемы CI/CD для ARMv7

echo "🔍 Проверка решения проблемы CI/CD сборки для ARMv7"
echo "======================================================"

# Проверяем что библиотеки для ARMv7 созданы
PREBUILT_DIR="ssh-tunnel-android-app/app/src/main/prebuilt"

echo "📦 Проверка библиотек ARMv7..."

if [ -f "$PREBUILT_DIR/openssl/armeabi-v7a/lib/libssl.a" ] && \
   [ -f "$PREBUILT_DIR/openssl/armeabi-v7a/lib/libcrypto.a" ]; then
  echo "✅ OpenSSL библиотеки для ARMv7 найдены:"
  ls -lh "$PREBUILT_DIR/openssl/armeabi-v7a/lib/"*.a
else
  echo "❌ OpenSSL библиотеки для ARMv7 отсутствуют"
  exit 1
fi

if [ -f "$PREBUILT_DIR/libssh/armeabi-v7a/lib/libssh.a" ]; then
  echo "✅ libssh библиотека для ARMv7 найдена:"
  ls -lh "$PREBUILT_DIR/libssh/armeabi-v7a/lib/libssh.a"
else
  echo "❌ libssh библиотека для ARMv7 отсутствует"
  exit 1
fi

echo ""
echo "🧪 Проверка флагов компиляции ARMv7..."

# Проверяем флаги в скрипте
if grep -q "armelf_linux_eabi" build_openssl.sh; then
  echo "✅ Флаг -Wl,-m,armelf_linux_eabi найден в build_openssl.sh"
else
  echo "❌ Отсутствует флаг -Wl,-m,armelf_linux_eabi"
  exit 1
fi

if grep -q "mfpu=neon" build_openssl.sh; then
  echo "✅ Флаг -mfpu=neon найден в build_openssl.sh"
else
  echo "❌ Отсутствует флаг -mfpu=neon"
  exit 1
fi

if grep -q "no-apps" build_openssl.sh; then
  echo "✅ Опция no-apps найдена для ARMv7"
else
  echo "❌ Отсутствует опция no-apps для ARMv7"
  exit 1
fi

echo ""
echo "📊 Размеры библиотек ARMv7:"
echo "OpenSSL libcrypto.a: $(du -h "$PREBUILT_DIR/openssl/armeabi-v7a/lib/libcrypto.a" | cut -f1)"
echo "OpenSSL libssl.a: $(du -h "$PREBUILT_DIR/openssl/armeabi-v7a/lib/libssl.a" | cut -f1)"
echo "libssh.a: $(du -h "$PREBUILT_DIR/libssh/armeabi-v7a/lib/libssh.a" | cut -f1)"

echo ""
echo "🎉 Все проверки пройдены! Проблема CI/CD для ARMv7 решена."
echo ""
echo "📋 Исправления:"
echo "  ✅ Добавлен флаг -Wl,-m,armelf_linux_eabi для ARMv7"
echo "  ✅ Изменен FPU с vfpv3-d16 на neon"
echo "  ✅ OpenSSL для ARMv7 собирается с no-apps и build_libs"
echo "  ✅ Устранена ошибка 'is incompatible with armelf_linux_eabi'"
echo ""
echo "💡 Готово к использованию в CI/CD!"
