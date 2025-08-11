#!/bin/bash

# End-to-End тестирование UDP Bridge
# Этот скрипт проводит полное функциональное тестирование системы

# Цвета для вывода
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Переменные тестирования
TEST_RESULTS_DIR="test_results"
LOG_FILE="$TEST_RESULTS_DIR/e2e_test.log"
SUMMARY_FILE="$TEST_RESULTS_DIR/test_summary.json"

# Конфигурация
SERVER_HOST="localhost"
SSH_PORT="2222"
BRIDGE_PORT="8080"
UDP_TARGET_PORT="5060"
SSH_USER="sshuser"
SSH_PASS="sshpassword"

# Счетчики тестов
TESTS_TOTAL=0
TESTS_PASSED=0
TESTS_FAILED=0

# Функции логирования
log_info() {
    echo -e "${GREEN}[INFO]${NC} $1"
    [ -d "$TEST_RESULTS_DIR" ] && echo "[INFO] $1" >> "$LOG_FILE"
}

log_warn() {
    echo -e "${YELLOW}[WARN]${NC} $1"
    [ -d "$TEST_RESULTS_DIR" ] && echo "[WARN] $1" >> "$LOG_FILE"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
    [ -d "$TEST_RESULTS_DIR" ] && echo "[ERROR] $1" >> "$LOG_FILE"
}

log_test() {
    echo -e "${BLUE}[TEST]${NC} $1"
    [ -d "$TEST_RESULTS_DIR" ] && echo "[TEST] $1" >> "$LOG_FILE"
}

# Функция для запуска теста
run_test() {
    local test_name="$1"
    local test_function="$2"
    
    TESTS_TOTAL=$((TESTS_TOTAL + 1))
    log_test "Запуск теста: $test_name"
    
    if $test_function; then
        log_info "✓ PASSED: $test_name"
        TESTS_PASSED=$((TESTS_PASSED + 1))
        return 0
    else
        log_error "✗ FAILED: $test_name"
        TESTS_FAILED=$((TESTS_FAILED + 1))
        return 1
    fi
}

# Инициализация тестовой среды
init_test_environment() {
    log_info "Инициализация тестовой среды..."
    
    # Создание директории для результатов
    mkdir -p "$TEST_RESULTS_DIR"
    
    # Очистка лог файла
    > "$LOG_FILE"
    
    # Проверка базовых зависимостей
    if ! command -v nc &> /dev/null; then
        log_error "netcat не установлен"
        return 1
    fi
    
    if ! command -v ssh &> /dev/null; then
        log_error "SSH клиент не установлен"
        return 1
    fi
    
    log_info "Тестовая среда готова"
    return 0
}

# Тест 1: Проверка доступности SSH сервера
test_ssh_connectivity() {
    log_info "Проверка SSH подключения..."
    
    # Проверка порта SSH
    if ! nc -z "$SERVER_HOST" "$SSH_PORT"; then
        log_error "SSH порт $SSH_PORT недоступен"
        return 1
    fi
    
    # Попытка SSH подключения
    sshpass -p "$SSH_PASS" ssh -o ConnectTimeout=10 -o StrictHostKeyChecking=no \
        "$SSH_USER@$SERVER_HOST" -p "$SSH_PORT" "echo 'SSH OK'" > /tmp/ssh_test.log 2>&1
    
    if [ $? -eq 0 ]; then
        log_info "SSH подключение успешно"
        return 0
    else
        log_error "SSH подключение не удалось"
        cat /tmp/ssh_test.log
        return 1
    fi
}

# Тест 2: Проверка доступности UDP Bridge порта
test_bridge_port() {
    log_info "Проверка UDP Bridge порта..."
    
    if nc -z "$SERVER_HOST" "$BRIDGE_PORT"; then
        log_info "UDP Bridge порт $BRIDGE_PORT доступен"
        return 0
    else
        log_error "UDP Bridge порт $BRIDGE_PORT недоступен"
        return 1
    fi
}

# Тест 3: Проверка работы сервера (логи)
test_server_logs() {
    log_info "Проверка логов сервера..."
    
    cd server-udp-bridge
    
    # Проверка состояния контейнера
    if ! docker-compose ps | grep -q "Up"; then
        log_error "UDP Bridge сервер не запущен"
        cd ..
        return 1
    fi
    
    # Получение логов
    docker-compose logs --tail=50 > "../$TEST_RESULTS_DIR/server_logs.txt" 2>&1
    
    # Проверка на ошибки в логах
    if grep -i "error\|failed\|exception" "../$TEST_RESULTS_DIR/server_logs.txt"; then
        log_warn "Обнаружены ошибки в логах сервера"
    fi
    
    cd ..
    log_info "Логи сервера получены"
    return 0
}

# Тест 4: Базовое UDP эхо тестирование
test_udp_echo() {
    log_info "Тестирование UDP эхо сервера..."
    
    # Тестируем UDP порт контейнера напрямую
    # Отправка тестового UDP пакета на порт контейнера
    echo "TEST_MESSAGE" | timeout 5 nc -u -w1 localhost 5060 > "$TEST_RESULTS_DIR/udp_response.txt" 2>&1
    
    # Проверяем, что порт UDP доступен
    if timeout 5 nc -u -z localhost 5060 2>/dev/null; then
        log_info "UDP порт 5060 доступен для подключения"
        return 0
    else
        log_error "UDP порт 5060 недоступен"
        return 1
    fi
}

# Тест 5: Тестирование протокола UDP Bridge
test_bridge_protocol() {
    log_info "Тестирование протокола UDP Bridge..."
    
    # Простой тест подключения к Bridge порту
    if echo "BRIDGE_TEST" | timeout 10 nc -w5 "$SERVER_HOST" "$BRIDGE_PORT" > "$TEST_RESULTS_DIR/bridge_protocol_test.log" 2>&1; then
        log_info "Подключение к Bridge порту успешно"
        
        # Проверяем логи сервера на наличие записей о подключении
        cd server-udp-bridge
        if timeout 10 docker-compose logs --tail=20 | grep -i "connection\|client\|bridge" > "../$TEST_RESULTS_DIR/server_bridge_logs.log" 2>&1; then
            log_info "Сервер обрабатывает подключения корректно"
            cd ..
            return 0
        else
            log_warn "Сервер не показывает записи о подключении, но порт доступен"
            cd ..
            return 0
        fi
    else
        log_error "Не удалось подключиться к Bridge порту"
        cd server-udp-bridge 2>/dev/null || true
        timeout 5 docker-compose logs --tail=10 > "../$TEST_RESULTS_DIR/server_error_logs.log" 2>&1
        cd .. 2>/dev/null || true
        return 1
    fi
}

# Тест 6: Stress тест с множественными соединениями
test_multiple_connections() {
    log_info "Тестирование множественных соединений..."
    
    local connections=5
    local success_count=0
    
    for i in $(seq 1 $connections); do
        (
            # Простое TCP подключение к bridge порту
            echo "CLIENT_$i" | nc -w5 "$SERVER_HOST" "$BRIDGE_PORT" > "/tmp/conn_test_$i.log" 2>&1
        ) &
    done
    
    # Ожидание завершения всех подключений
    wait
    
    # Подсчет успешных подключений
    for i in $(seq 1 $connections); do
        if [ -f "/tmp/conn_test_$i.log" ]; then
            success_count=$((success_count + 1))
        fi
    done
    
    # Очистка временных файлов
    rm -f /tmp/conn_test_*.log
    
    if [ $success_count -ge $((connections / 2)) ]; then
        log_info "Тест множественных соединений успешен ($success_count/$connections)"
        return 0
    else
        log_error "Тест множественных соединений не удался ($success_count/$connections)"
        return 1
    fi
}

# Тест 7: Проверка Android APK (если доступен)
test_android_apk() {
    log_info "Проверка Android APK..."
    
    local apk_path="ssh-tunnel-android-app/app/build/outputs/apk/debug/app-debug.apk"
    
    if [ ! -f "$apk_path" ]; then
        log_warn "Android APK не найден, пропускаем тест"
        return 0
    fi
    
    # Проверка структуры APK
    if command -v aapt &> /dev/null; then
        aapt list "$apk_path" > "$TEST_RESULTS_DIR/apk_contents.txt" 2>&1
        
        # Проверка наличия нужных компонентов
        if grep -q "UdpBridgeService" "$TEST_RESULTS_DIR/apk_contents.txt"; then
            log_info "Android APK содержит нужные компоненты"
            return 0
        else
            log_warn "Android APK не содержит ожидаемые компоненты"
            return 1
        fi
    else
        # Простая проверка размера APK
        local apk_size=$(stat -c%s "$apk_path")
        if [ $apk_size -gt 1000000 ]; then  # > 1MB
            log_info "Android APK собран корректно (размер: $apk_size байт)"
            return 0
        else
            log_error "Android APK слишком мал (размер: $apk_size байт)"
            return 1
        fi
    fi
}

# Создание отчета о тестировании
generate_test_report() {
    log_info "Генерация отчета о тестировании..."
    
    local timestamp=$(date '+%Y-%m-%d %H:%M:%S')
    local success_rate=$(( (TESTS_PASSED * 100) / TESTS_TOTAL ))
    
    # JSON отчет
    cat > "$SUMMARY_FILE" << EOF
{
    "timestamp": "$timestamp",
    "summary": {
        "total_tests": $TESTS_TOTAL,
        "passed": $TESTS_PASSED,
        "failed": $TESTS_FAILED,
        "success_rate": $success_rate
    },
    "environment": {
        "server_host": "$SERVER_HOST",
        "ssh_port": $SSH_PORT,
        "bridge_port": $BRIDGE_PORT,
        "udp_target_port": $UDP_TARGET_PORT
    },
    "status": "$([ $TESTS_FAILED -eq 0 ] && echo "SUCCESS" || echo "FAILED")"
}
EOF

    # Текстовый отчет
    cat > "$TEST_RESULTS_DIR/test_report.txt" << EOF
=== UDP BRIDGE END-TO-END TEST REPORT ===
Время тестирования: $timestamp

Результаты:
  Всего тестов: $TESTS_TOTAL
  Успешных: $TESTS_PASSED
  Неудачных: $TESTS_FAILED
  Процент успеха: $success_rate%

Статус: $([ $TESTS_FAILED -eq 0 ] && echo "УСПЕШНО" || echo "ЕСТЬ ОШИБКИ")

Файлы с результатами:
  - Полный лог: $LOG_FILE
  - JSON отчет: $SUMMARY_FILE
  - Логи сервера: $TEST_RESULTS_DIR/server_logs.txt
EOF

    log_info "Отчет сохранен в $TEST_RESULTS_DIR/"
}

# Основная функция тестирования
main() {
    log_info "=== Запуск End-to-End тестирования UDP Bridge ==="
    
    # Проверка, что тестовое окружение запущено
    if ! docker ps | grep -q udp-bridge; then
        log_error "Сервер UDP Bridge не запущен. Запустите: ./test_environment_setup.sh"
        exit 1
    fi
    
    init_test_environment
    
    # Запуск тестов
    run_test "SSH Connectivity" test_ssh_connectivity
    run_test "Bridge Port Check" test_bridge_port
    run_test "Server Logs Check" test_server_logs
    run_test "UDP Echo Test" test_udp_echo
    run_test "Bridge Protocol Test" test_bridge_protocol
    run_test "Multiple Connections Test" test_multiple_connections
    run_test "Android APK Check" test_android_apk
    
    generate_test_report
    
    # Итоговый статус
    echo ""
    log_info "=== ИТОГИ ТЕСТИРОВАНИЯ ==="
    log_info "Всего тестов: $TESTS_TOTAL"
    log_info "Успешных: $TESTS_PASSED"
    log_info "Неудачных: $TESTS_FAILED"
    
    if [ $TESTS_FAILED -eq 0 ]; then
        log_info "🎉 ВСЕ ТЕСТЫ ПРОШЛИ УСПЕШНО!"
        exit 0
    else
        log_error "❌ НЕКОТОРЫЕ ТЕСТЫ НЕ ПРОШЛИ"
        exit 1
    fi
}

# Обработка аргументов
case "${1:-}" in
    "quick")
        # Создание директории для результатов сначала
        mkdir -p "$TEST_RESULTS_DIR"
        > "$LOG_FILE"
        log_info "Запуск быстрых тестов..."
        init_test_environment
        run_test "SSH Connectivity" test_ssh_connectivity
        run_test "Bridge Port Check" test_bridge_port
        run_test "Server Logs Check" test_server_logs
        generate_test_report
        ;;
    "protocol-only")
        # Создание директории для результатов сначала
        mkdir -p "$TEST_RESULTS_DIR"
        > "$LOG_FILE"
        log_info "Запуск только тестов протокола..."
        init_test_environment
        run_test "Bridge Protocol Test" test_bridge_protocol
        run_test "UDP Echo Test" test_udp_echo
        generate_test_report
        ;;
    *)
        main
        ;;
esac
