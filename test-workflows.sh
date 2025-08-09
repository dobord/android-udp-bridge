#!/bin/bash

# Скрипт для локального тестирования GitHub Actions workflows
# Требует установки act: https://github.com/nektos/act

set -e

# Цвета для вывода
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Функция для вывода цветного текста
print_color() {
    echo -e "${1}${2}${NC}"
}

print_header() {
    echo "================================"
    print_color $BLUE "$1"
    echo "================================"
}

# Проверка наличия act
if ! command -v act &> /dev/null; then
    print_color $RED "❌ act не установлен!"
    echo "Установите act: https://github.com/nektos/act#installation"
    echo "Например: brew install act"
    exit 1
fi

# Проверка наличия Docker
if ! command -v docker &> /dev/null; then
    print_color $RED "❌ Docker не установлен!"
    echo "Установите Docker для запуска act"
    exit 1
fi

print_header "🚀 Локальное тестирование GitHub Actions"

echo "Доступные команды:"
echo "1. test-pr       - Тестировать PR check workflow"
echo "2. test-build    - Тестировать build workflow"
echo "3. test-release  - Тестировать release workflow (с fake tag)"
echo "4. list          - Показать все доступные workflows"
echo "5. validate      - Проверить синтаксис всех workflows"

if [ $# -eq 0 ]; then
    echo ""
    print_color $YELLOW "Использование: $0 [команда]"
    exit 1
fi

case $1 in
    "test-pr")
        print_header "🔍 Тестирование PR Check"
        act pull_request -W .github/workflows/pr-check.yml \
            --container-architecture linux/amd64 \
            --artifact-server-path /tmp/artifacts \
            -v
        ;;
        
    "test-build")
        print_header "🏗️ Тестирование Build and Release"
        act push -W .github/workflows/build-and-release.yml \
            --container-architecture linux/amd64 \
            --artifact-server-path /tmp/artifacts \
            -v
        ;;
        
    "test-release")
        print_header "📦 Тестирование Release"
        # Создаем временный тег для тестирования
        git tag -f v99.99.99-test 2>/dev/null || true
        
        act push -W .github/workflows/release.yml \
            --container-architecture linux/amd64 \
            --artifact-server-path /tmp/artifacts \
            -e .github/test-events/tag-push.json \
            -v
            
        # Удаляем тестовый тег
        git tag -d v99.99.99-test 2>/dev/null || true
        ;;
        
    "list")
        print_header "📋 Доступные Workflows"
        act -l
        ;;
        
    "validate")
        print_header "✅ Валидация Workflows"
        
        # Проверяем синтаксис YAML файлов
        for file in .github/workflows/*.yml; do
            if [ -f "$file" ]; then
                echo "Проверка $file..."
                
                # Простая проверка YAML синтаксиса
                if python3 -c "import yaml; yaml.safe_load(open('$file'))" 2>/dev/null; then
                    print_color $GREEN "✅ $file - OK"
                else
                    print_color $RED "❌ $file - ОШИБКА СИНТАКСИСА"
                fi
            fi
        done
        
        # Проверяем с помощью act
        echo ""
        echo "Проверка с помощью act..."
        if act -l >/dev/null 2>&1; then
            print_color $GREEN "✅ Все workflows валидны"
        else
            print_color $RED "❌ Обнаружены ошибки в workflows"
        fi
        ;;
        
    "setup")
        print_header "⚙️ Настройка окружения для тестирования"
        
        # Создаем директории для тестовых событий
        mkdir -p .github/test-events
        
        # Создаем файл события для тестирования тегов
        cat > .github/test-events/tag-push.json << 'EOF'
{
  "ref": "refs/tags/v99.99.99-test",
  "ref_name": "v99.99.99-test",
  "repository": {
    "name": "android-udp-bridge",
    "full_name": "dobord/android-udp-bridge"
  }
}
EOF

        # Создаем .actrc для настроек по умолчанию
        cat > .actrc << 'EOF'
--container-architecture linux/amd64
--artifact-server-path /tmp/artifacts
--env-file .github/.env.test
EOF

        # Создаем тестовый файл переменных окружения
        cat > .github/.env.test << 'EOF'
ANDROID_HOME=/opt/android-sdk
ANDROID_ABI=arm64-v8a
GITHUB_TOKEN=fake-token-for-testing
EOF

        print_color $GREEN "✅ Настройка завершена!"
        echo "Файлы созданы:"
        echo "  - .github/test-events/tag-push.json"
        echo "  - .actrc"
        echo "  - .github/.env.test"
        ;;
        
    "clean")
        print_header "🧹 Очистка тестовых файлов"
        
        rm -rf /tmp/artifacts
        rm -f .github/test-events/tag-push.json
        rm -f .actrc
        rm -f .github/.env.test
        
        print_color $GREEN "✅ Очистка завершена"
        ;;
        
    *)
        print_color $RED "❌ Неизвестная команда: $1"
        echo ""
        echo "Доступные команды: test-pr, test-build, test-release, list, validate, setup, clean"
        exit 1
        ;;
esac

print_color $GREEN "✅ Команда '$1' выполнена"
