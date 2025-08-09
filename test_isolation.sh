#!/bin/bash

# Скрипт для проверки изолированности сборки разных архитектур

set -e

echo "🧪 Тестируем изолированность сборки для разных архитектур"

# Флаг для отслеживания ошибок
FAILED=0

# Симулируем параллельную сборку для двух архитектур
ARCHS=("arm64-v8a" "x86_64")

for ARCH in "${ARCHS[@]}"; do
    echo "🔄 Запускаем тест для архитектуры: $ARCH"
    
    # Создаём процесс в фоне для симуляции параллельной сборки
    (
        export ANDROID_ABI="$ARCH"
        export ARCH_WORK_DIR="$(pwd)/test_build_$ARCH"
        export USE_FILE_LOCKS=1
        
        # Создаём изолированную директорию
        mkdir -p "$ARCH_WORK_DIR"
        
        echo "[$ARCH] Рабочая директория: $ARCH_WORK_DIR"
        echo "[$ARCH] Переменная ANDROID_ABI: $ANDROID_ABI"
        
        # Проверяем, что директории действительно разные
        if [ "$ARCH" = "arm64-v8a" ]; then
            touch "$ARCH_WORK_DIR/test_arm64_marker"
            sleep 2
            if [ -f "$(pwd)/test_build_x86_64/test_x86_64_marker" ]; then
                echo "❌ [$ARCH] Обнаружен файл от другой архитектуры - изоляция нарушена!"
                exit 1
            else
                echo "✅ [$ARCH] Изоляция работает корректно"
            fi
        else
            touch "$ARCH_WORK_DIR/test_x86_64_marker"
            sleep 2
            if [ -f "$(pwd)/test_build_arm64-v8a/test_arm64_marker" ]; then
                echo "❌ [$ARCH] Обнаружен файл от другой архитектуры - изоляция нарушена!"
                exit 1
            else
                echo "✅ [$ARCH] Изоляция работает корректно"
            fi
        fi
        
        echo "[$ARCH] Тест завершен успешно"
        
    ) &
    
    # Сохраняем PID процесса
    PIDS[$ARCH]=$!
done

# Ждём завершения всех фоновых процессов и проверяем коды возврата
for ARCH in "${ARCHS[@]}"; do
    wait ${PIDS[$ARCH]}
    if [ $? -ne 0 ]; then
        echo "❌ Тест для архитектуры $ARCH завершился с ошибкой"
        FAILED=1
    fi
done

if [ $FAILED -eq 0 ]; then
    echo "🎉 Все тесты изолированности пройдены успешно!"
else
    echo "💥 Некоторые тесты изолированности провалились!"
fi

# Очищаем тестовые файлы
rm -rf test_build_*

echo "🧹 Тестовые файлы очищены"

exit $FAILED
