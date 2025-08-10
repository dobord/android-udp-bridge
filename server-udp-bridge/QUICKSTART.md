# UDP Bridge Server - Quick Start Guide

## Сборка и запуск

### 1. Сборка сервера
```bash
# Сборка сервера
make server

# Или сборка в debug режиме
make debug

# Очистка
make clean
```

### 2. Запуск сервера

#### Простой запуск с настройками по умолчанию:
```bash
./udp-bridge-server
```

#### Запуск с параметрами:
```bash
./udp-bridge-server -p 9090 -t example.com:5060 -m 500 -T 600
```

#### Использование debug скрипта:
```bash
# Запуск в foreground режиме
./start_debug.sh

# Запуск в background режиме
./start_debug.sh -d

# Запуск с кастомными настройками
./start_debug.sh -p 9090 -t example.com:5060 -v
```

### 3. Конфигурация через environment variables

```bash
export BRIDGE_TCP_PORT=8080
export TARGET_UDP_HOST=localhost
export TARGET_UDP_PORT=5060
export MAX_CLIENTS=1000
export CLIENT_TIMEOUT=300

./udp-bridge-server
```

### 4. Docker развертывание

```bash
# Сборка и запуск
docker-compose up --build

# Запуск в background
docker-compose up -d

# Просмотр логов
docker-compose logs -f

# Остановка
docker-compose down
```

### 5. Тестирование

```bash
# Базовый тест функциональности
./test_server.sh

# Тест с кастомными параметрами
./test_server.sh -h localhost -p 8080 -t 5060
```

## Параметры командной строки

| Параметр | Описание | По умолчанию |
|----------|----------|--------------|
| `-p <port>` | TCP порт для прослушивания | 8080 |
| `-t <host:port>` | Целевой UDP сервер | localhost:5060 |
| `-m <max>` | Максимальное количество клиентов | 1000 |
| `-T <timeout>` | Таймаут клиента в секундах | 300 |
| `-h` | Показать справку | - |

## Environment Variables

| Переменная | Описание |
|------------|----------|
| `BRIDGE_TCP_PORT` | TCP порт для прослушивания |
| `TARGET_UDP_HOST` | Хост целевого UDP сервера |
| `TARGET_UDP_PORT` | Порт целевого UDP сервера |
| `MAX_CLIENTS` | Максимальное количество клиентов |
| `CLIENT_TIMEOUT` | Таймаут клиента в секундах |
| `LOG_LEVEL` | Уровень логирования (INFO, DEBUG) |

## Docker Compose конфигурация

```yaml
environment:
  - TARGET_UDP_HOST=your-target-host
  - TARGET_UDP_PORT=5060
  - BRIDGE_TCP_PORT=8080
  - CLIENT_TIMEOUT=300
  - MAX_CLIENTS=1000

ports:
  - "2222:22"           # SSH
  - "8080:8080"         # Bridge TCP
  - "5060:5060/udp"     # UDP forwarding
```

## Архитектура

Сервер состоит из следующих компонентов:

1. **Protocol Handler** (`protocol.c`) - обработка протокольных сообщений
2. **Client Table** (`client_table.c`) - управление подключенными клиентами
3. **UDP Forwarder** (`udp_forwarder.c`) - пересылка UDP пакетов
4. **Main Server** (`main.c`) - основной TCP сервер и интеграция

## Логи

- При запуске через Docker: `/opt/udp-bridge/logs/`
- При запуске через debug скрипт: `./logs/`
- При запуске вручную: stdout/stderr

## Статус реализации

✅ **Фаза 1.5 ЗАВЕРШЕНА**

- [x] Создан `main.c` с TCP сервером
- [x] Интеграция всех компонентов
- [x] Конфигурация через environment variables  
- [x] Логирование и debugging
- [x] Docker интеграция
- [x] Тестовые скрипты

**Критерий готовности выполнен:** Сервер может принимать TCP соединения, парсить протокол и форвардить UDP пакеты.

## Следующие шаги

Переход к **Фазе 2**: Модификация Android приложения для работы с новым протоколом.
