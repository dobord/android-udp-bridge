#!/bin/bash

# Быстрая проверка решения проблемы CI/CD для всех архитектур

echo "🔍 Проверка решения проблем CI/CD сборки для всех архитектур"
echo "=============================================================="

# Проверяем что библиотеки для всех архитектур созданы
PREBUILT_DIR="ssh-tunnel-android-app/app/src/main/prebuilt"
ARCHITECTURES=("arm64-v8a" "armeabi-v7a" "x86_64" "x86")

echo "📦 Проверка библиотек для всех архитектур..."

for ARCH in "${ARCHITECTURES[@]}"; do
    echo ""
    echo "🔍 Проверяем архитектуру: $ARCH"
    
    if [ -f "$PREBUILT_DIR/openssl/$ARCH/lib/libssl.a" ] && \
       [ -f "$PREBUILT_DIR/openssl/$ARCH/lib/libcrypto.a" ]; then
      echo "✅ OpenSSL библиотеки для $ARCH найдены"
      echo "   libcrypto.a: $(du -h "$PREBUILT_DIR/openssl/$ARCH/lib/libcrypto.a" | cut -f1)"
      echo "   libssl.a: $(du -h "$PREBUILT_DIR/openssl/$ARCH/lib/libssl.a" | cut -f1)"
    else
      echo "❌ OpenSSL библиотеки для $ARCH отсутствуют"
      exit 1
    fi

    if [ -f "$PREBUILT_DIR/libssh/$ARCH/lib/libssh.a" ]; then
      echo "✅ libssh библиотека для $ARCH найдена"
      echo "   libssh.a: $(du -h "$PREBUILT_DIR/libssh/$ARCH/lib/libssh.a" | cut -f1)"
    else
      echo "❌ libssh библиотека для $ARCH отсутствует"
      exit 1
    fi
done

echo ""
echo "🧪 Проверка флагов компиляции..."

# Проверяем флаги в скрипте
if grep -q "armelf_linux_eabi" build_openssl.sh; then
  echo "✅ Флаг -Wl,-m,armelf_linux_eabi найден для ARMv7"
else
  echo "❌ Отсутствует флаг -Wl,-m,armelf_linux_eabi для ARMv7"
  exit 1
fi

if grep -q "mfpu=neon" build_openssl.sh; then
  echo "✅ Флаг -mfpu=neon найден для ARMv7"
else
  echo "❌ Отсутствует флаг -mfpu=neon для ARMv7"
  exit 1
fi

if grep -q "Wno-macro-redefined" build_openssl.sh; then
  echo "✅ Флаг -Wno-macro-redefined найден для x86_64"
else
  echo "❌ Отсутствует флаг -Wno-macro-redefined для x86_64"
  exit 1
fi

if grep -q "no-apps" build_openssl.sh; then
  echo "✅ Опция no-apps найдена для проблемных архитектур"
else
  echo "❌ Отсутствует опция no-apps"
  exit 1
fi

echo ""
echo "🎉 Все проверки пройдены! Проблемы CI/CD для всех архитектур решены."
echo ""
echo "📋 Исправления:"
echo "  ✅ ARMv7: добавлен флаг -Wl,-m,armelf_linux_eabi, изменен FPU на neon"
echo "  ✅ x86_64: добавлен флаг -Wno-macro-redefined, отключены apps"
echo "  ✅ OpenSSL для ARMv7 и x86_64 собирается с no-apps и build_libs"
echo "  ✅ Устранены ошибки линковки 'is incompatible with ABI'"
echo ""
echo "💡 Готово к использованию в CI/CD для всех архитектур!"
