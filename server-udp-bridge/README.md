# (Legacy) UDP Bridge Server

> Этот сервер относится к устаревшей реализации кастомного протокола (UDP Bridge). В актуальной архитектуре используется `udp2tcp` сервер. См. `MIGRATION_UDP2TCP.md` и основной `README.md`.

## Архитектура

```
[UDP Client] ←→ [Android UDP Bridge] ←→ [SSH TCP Tunnel] ←→ [Server UDP Bridge] ←→ [Target UDP Server]
```

## Быстрый старт

### 1. Настройка SSH ключей
```bash
make setup-ssh
```

### 2. Запуск сервера
```bash
make up
```

### 3. Проверка работы
```bash
make test
```

## Конфигурация

Основные настройки находятся в файле `.env`:

```bash
# Целевой UDP сервер
TARGET_UDP_HOST=127.0.0.1
TARGET_UDP_PORT=5060

# Настройки bridge сервера
BRIDGE_TCP_PORT=8080
CLIENT_TIMEOUT=300
MAX_CLIENTS=1000
```

## Порты

- **22** - SSH сервер
- **8080** - TCP порт для подключения Android клиентов
- **5060/udp** - Проброшенный UDP порт (пример для SIP)

## Команды Makefile

- `make setup-ssh` - Генерация SSH ключей
- `make build` - Сборка Docker образа
- `make up` - Запуск сервисов
- `make down` - Остановка сервисов
- `make logs` - Просмотр логов
- `make clean` - Очистка контейнеров и образов
- `make test` - Тестирование подключения

## Структура проекта

```
server-udp-bridge/
├── Dockerfile              # Docker образ
├── docker-compose.yml      # Конфигурация сервисов
├── Makefile                # Команды для сборки и запуска
├── .env                    # Переменные окружения
├── src/                    # Исходный код сервера
│   ├── main.c             # Основной сервер
│   ├── protocol.h/c       # Протокол сообщений
│   ├── client_table.h/c   # Управление клиентами
│   └── udp_forwarder.h/c  # UDP форвардинг
├── config/                 # Конфигурационные файлы
│   ├── sshd_config        # Настройки SSH сервера
│   └── supervisord.conf   # Настройки supervisor
├── scripts/               # Скрипты запуска
│   ├── entrypoint.sh      # Точка входа контейнера
│   └── setup_ssh.sh       # Настройка SSH ключей
├── ssh_keys/              # SSH ключи (генерируются автоматически)
└── logs/                  # Логи сервера
```

## Протокол (Legacy)

Сервер использует собственный протокол для мультиплексирования UDP соединений (устарело, заменяется udp2tcp):

```c
typedef struct {
    char magic[4];          // "UDPB"
    uint8_t version;        // 1
    uint8_t message_type;   // MSG_DATA, MSG_CLIENT_REGISTER, etc.
    uint16_t flags;         // Зарезервировано
    uint32_t client_id;     // Идентификатор клиента
    uint32_t payload_size;  // Размер данных
    uint32_t checksum;      // CRC32 заголовка
} udp_bridge_header_t;
```

## Логирование

Логи находятся в директории `logs/`:
- `supervisord.log` - Логи supervisor
- `sshd.log` - Логи SSH сервера
- `udp-bridge.log` - Логи UDP bridge сервера

## Отладка

Для отладки используйте:
```bash
make debug
```

Для локальной компиляции:
```bash
make compile
```

## Безопасность

- SSH сервер настроен на использование ключей
- Пароль аутентификация отключена в production
- Пользователь `sshuser` ограничен в правах
- Логирование всех подключений
