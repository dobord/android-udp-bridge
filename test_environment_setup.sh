#!/bin/bash

# Тестовое окружение для End-to-End тестирования UDP Bridge
# Этот скрипт настраивает полное тестовое окружение

set -e

echo "=== Настройка тестового окружения UDP Bridge ==="

# Цвета для вывода
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Функция для логирования
log_info() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

log_warn() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Проверка зависимостей
check_dependencies() {
    log_info "Проверка зависимостей..."
    
    if ! command -v docker &> /dev/null; then
        log_error "Docker не установлен"
        exit 1
    fi
    
    if ! command -v docker-compose &> /dev/null; then
        log_error "Docker Compose не установлен"
        exit 1
    fi
    
    if ! command -v adb &> /dev/null; then
        log_warn "ADB не найден. Android тестирование будет недоступно"
    fi
    
    log_info "Зависимости проверены"
}

# Остановка существующих контейнеров
cleanup_existing() {
    log_info "Остановка существующих контейнеров..."
    cd server-udp-bridge
    docker-compose down --remove-orphans || true
    cd ..
}

# Сборка сервера
build_server() {
    log_info "Сборка UDP Bridge сервера..."
    cd server-udp-bridge
    
    # Проверяем, что сервер собран
    if [ ! -f "udp-bridge-server" ]; then
        log_info "Сборка сервера из исходников..."
        make clean && make
    fi
    
    # Сборка Docker образа
    log_info "Сборка Docker образа..."
    docker-compose build
    
    cd ..
    log_info "Сервер собран успешно"
}

# Запуск сервера
start_server() {
    log_info "Запуск UDP Bridge сервера..."
    cd server-udp-bridge
    
    # Запуск в фоновом режиме
    docker-compose up -d
    
    # Ожидание готовности
    log_info "Ожидание готовности сервера..."
    sleep 10
    
    # Проверка статуса
    if docker-compose ps | grep -q "Up"; then
        log_info "Сервер запущен успешно"
    else
        log_error "Не удалось запустить сервер"
        docker-compose logs
        exit 1
    fi
    
    cd ..
}

# Проверка Android SDK
check_android_sdk() {
    log_info "Проверка Android SDK..."
    
    if [ -z "$ANDROID_HOME" ]; then
        log_warn "ANDROID_HOME не установлен"
        # Попытка найти SDK автоматически
        if [ -d "$HOME/Android/Sdk" ]; then
            export ANDROID_HOME="$HOME/Android/Sdk"
            log_info "Найден Android SDK: $ANDROID_HOME"
        else
            log_error "Android SDK не найден. Установите Android SDK и настройте ANDROID_HOME"
            return 1
        fi
    fi
    
    if [ ! -d "$ANDROID_HOME" ]; then
        log_error "Android SDK не найден по пути: $ANDROID_HOME"
        return 1
    fi
    
    log_info "Android SDK готов"
    return 0
}

# Сборка Android APK
build_android_apk() {
    log_info "Сборка Android APK..."
    cd ssh-tunnel-android-app
    
    # Проверка Gradle wrapper
    if [ ! -f "gradlew" ]; then
        log_error "Gradle wrapper не найден"
        cd ..
        return 1
    fi
    
    # Очистка и сборка
    ./gradlew clean
    ./gradlew assembleDebug
    
    # Проверка результата
    APK_PATH="app/build/outputs/apk/debug/app-debug.apk"
    if [ -f "$APK_PATH" ]; then
        log_info "APK собран успешно: $APK_PATH"
        cd ..
        return 0
    else
        log_error "Не удалось собрать APK"
        cd ..
        return 1
    fi
}

# Создание тестовых данных
create_test_data() {
    log_info "Создание тестовых данных..."
    
    # Создание директории для тестов
    mkdir -p test_results
    
    # Создание конфигурации тестов
    cat > test_results/test_config.json << EOF
{
    "server": {
        "host": "localhost",
        "ssh_port": 2222,
        "bridge_port": 8080,
        "udp_target_port": 5060
    },
    "client": {
        "local_udp_port": 15060,
        "ssh_user": "sshuser",
        "ssh_password": "sshpassword"
    },
    "tests": {
        "basic_connectivity": true,
        "udp_forwarding": true,
        "multiple_clients": true,
        "stress_test": false
    }
}
EOF

    log_info "Тестовые данные созданы"
}

# Показать статус окружения
show_environment_status() {
    log_info "=== Статус тестового окружения ==="
    
    echo "Docker контейнеры:"
    cd server-udp-bridge
    docker-compose ps
    cd ..
    
    echo ""
    echo "Порты:"
    echo "  SSH сервер: localhost:2222"
    echo "  UDP Bridge: localhost:8080"
    echo "  UDP Target: localhost:5060"
    
    echo ""
    echo "Тестовые учетные данные:"
    echo "  SSH пользователь: sshuser"
    echo "  SSH пароль: sshpassword"
    
    if [ -f "ssh-tunnel-android-app/app/build/outputs/apk/debug/app-debug.apk" ]; then
        echo ""
        echo "Android APK: готов"
    else
        echo ""
        echo "Android APK: не собран"
    fi
}

# Основная функция
main() {
    log_info "Начинаем настройку тестового окружения..."
    
    check_dependencies
    cleanup_existing
    build_server
    start_server
    create_test_data
    
    if check_android_sdk; then
        if build_android_apk; then
            log_info "Android APK собран успешно"
        else
            log_warn "Не удалось собрать Android APK. End-to-end тестирование будет ограничено"
        fi
    else
        log_warn "Android SDK недоступен. Пропускаем сборку APK"
    fi
    
    show_environment_status
    
    log_info "=== Тестовое окружение готово! ==="
    log_info "Для запуска тестов используйте: ./run_e2e_tests.sh"
}

# Обработка аргументов командной строки
case "${1:-}" in
    "cleanup")
        cleanup_existing
        ;;
    "server-only")
        check_dependencies
        cleanup_existing
        build_server
        start_server
        show_environment_status
        ;;
    "android-only")
        check_android_sdk && build_android_apk
        ;;
    *)
        main
        ;;
esac
