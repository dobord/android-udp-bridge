# План реализации новой архитектуры UDP Bridge

## Фаза 1: Разработка Server UDP Bridge (1-2 недели)

### 1.1 Создание базовой структуры проекта
- [x] Создать директорию `server-udp-bridge/`
- [x] Настроить Docker окружение
- [x] Создать Dockerfile с SSH сервером
- [x] Настроить docker-compose.yml

### 1.2 Реализация протокола
- [x] Создать `protocol.h` с определениями структур
- [x] Реализовать `protocol.c` с функциями парсинга
- [x] Добавить валидацию и checksum
- [x] Создать unit тесты протокола

### 1.3 Управление клиентами
- [x] Реализовать `client_table.h/c`
- [x] Добавить функции добавления/удаления клиентов
- [x] Реализовать автоматические таймауты
- [x] Добавить статистику и мониторинг

### 1.4 UDP Forwarder
- [x] Создать `udp_forwarder.h/c`
- [x] Реализовать forwarding к целевому серверу
- [x] Добавить mapping ответов обратно к клиентам
- [x] Обработка ошибок и reconnect

### 1.5 Main сервер
- [x] Создать `main.c` с TCP сервером
- [x] Интеграция всех компонентов
- [x] Конфигурация через environment variables
- [x] Логирование и debugging

**Критерий готовности:** Сервер может принимать TCP соединения, парсить протокол и форвардить UDP пакеты.

## Фаза 2: Модификация Android приложения (1-2 недели)

### 2.1 Новый протокол в Android
- [x] Портировать `protocol.h` в Android NDK
- [x] Создать `udp_bridge_protocol.c`
- [x] Интегрировать с существующим JNI кодом
- [x] Добавить протокольные функции

### 2.2 Client Manager
- [ ] Создать `client_manager.h/c`
- [ ] Реализовать таблицу клиентов
- [ ] Добавить автоматическое присвоение client_id
- [ ] Управление timeouts

### 2.3 Модификация UDP Listener
- [ ] Изменить существующий UDP код
- [ ] Добавить client identification
- [ ] Интеграция с protocol handler
- [ ] Сохранить обратную совместимость

### 2.4 TCP Connection Manager
- [ ] Модифицировать SSH туннель код
- [ ] Добавить протокольную обертку
- [ ] Реализовать reconnection logic
- [ ] Обработка протокольных ответов

### 2.5 Java интерфейс
- [ ] Создать `UdpBridgeConfig.java`
- [ ] Модифицировать `UdpBridgeService.java`
- [ ] Добавить UI для новых настроек
- [ ] Переключатель между старой/новой архитектурой

**Критерий готовности:** Android приложение может подключаться к серверу и передавать UDP через новый протокол.

## Фаза 3: Интеграция и тестирование (1 неделя)

### 3.1 End-to-end тестирование
- [ ] Настроить тестовое окружение
- [ ] Развернуть server bridge в Docker
- [ ] Собрать и установить Android APK
- [ ] Провести базовые функциональные тесты

### 3.2 Performance тестирование
- [ ] Тесты с одним клиентом
- [ ] Тесты с множественными клиентами
- [ ] Измерение latency и throughput
- [ ] Сравнение с текущей архитектурой

### 3.3 Stress тестирование
- [ ] Тесты с большим количеством клиентов
- [ ] Тесты на длительную работу
- [ ] Тесты переподключения
- [ ] Memory leak тестирование

**Критерий готовности:** Система стабильно работает под нагрузкой и показывает улучшения по сравнению с текущей архитектурой.

## Фаза 4: Документация и развертывание (0.5 недели)

### 4.1 Документация
- [ ] Обновить README с новой архитектурой
- [ ] Создать deployment guide
- [ ] Добавить troubleshooting guide
- [ ] Документировать API протокола

### 4.2 CI/CD интеграция
- [ ] Добавить сборку server bridge в GitHub Actions
- [ ] Настроить Docker image publishing
- [ ] Добавить автоматические тесты
- [ ] Integration с существующим Android CI

**Критерий готовности:** Полная документация и автоматизация развертывания.

## Детальные задачи по приоритету

### Высокий приоритет (Критический путь)

#### Задача 1: Протокол и базовые структуры
```c
// server-udp-bridge/src/protocol.h
#ifndef UDP_BRIDGE_PROTOCOL_H
#define UDP_BRIDGE_PROTOCOL_H

#define UDP_BRIDGE_MAGIC "UDPB"
#define UDP_BRIDGE_VERSION 1

typedef enum {
    MSG_DATA = 0x01,
    MSG_CLIENT_REGISTER = 0x02,
    MSG_CLIENT_TIMEOUT = 0x03,
    MSG_PING = 0x04,
    MSG_PONG = 0x05
} message_type_t;

typedef struct {
    char magic[4];          // "UDPB"
    uint8_t version;        // 1
    uint8_t message_type;   // message_type_t
    uint16_t flags;         // Reserved
    uint32_t client_id;     // Client identifier
    uint32_t payload_size;  // Size of payload
    uint32_t checksum;      // CRC32 of header
} __attribute__((packed)) udp_bridge_header_t;

// Function prototypes
int parse_header(const char* buffer, udp_bridge_header_t* header);
int create_message(char* buffer, message_type_t type, uint32_t client_id, 
                  const void* payload, uint32_t payload_size);
uint32_t calculate_checksum(const udp_bridge_header_t* header);

#endif
```

#### Задача 2: Client table для сервера
```c
// server-udp-bridge/src/client_table.h
#ifndef CLIENT_TABLE_H
#define CLIENT_TABLE_H

typedef struct client_entry {
    uint32_t client_id;
    time_t last_activity;
    uint64_t bytes_received;
    uint64_t bytes_sent;
    struct client_entry* next;
} client_entry_t;

typedef struct {
    client_entry_t* head;
    uint32_t count;
    uint32_t next_id;
    pthread_mutex_t mutex;
} client_table_t;

// Function prototypes
client_table_t* client_table_create(void);
void client_table_destroy(client_table_t* table);
uint32_t client_table_add(client_table_t* table);
client_entry_t* client_table_find(client_table_t* table, uint32_t client_id);
void client_table_remove_expired(client_table_t* table, time_t timeout);
void client_table_update_activity(client_table_t* table, uint32_t client_id);

#endif
```

#### Задача 3: UDP Forwarder
```c
// server-udp-bridge/src/udp_forwarder.h
#ifndef UDP_FORWARDER_H
#define UDP_FORWARDER_H

typedef struct {
    int socket;
    struct sockaddr_in target_addr;
    client_table_t* clients;
    int tcp_socket;  // Connection back to Android
} udp_forwarder_t;

// Function prototypes
udp_forwarder_t* udp_forwarder_create(const char* target_host, 
                                     int target_port, client_table_t* clients);
void udp_forwarder_destroy(udp_forwarder_t* forwarder);
int udp_forwarder_send(udp_forwarder_t* forwarder, uint32_t client_id, 
                      const void* data, size_t size);
void* udp_forwarder_thread(void* arg);

#endif
```

### Средний приоритет

#### Задача 4: Dockerfile для сервера
```dockerfile
# server-udp-bridge/Dockerfile
FROM ubuntu:22.04

# Install dependencies
RUN apt-get update && apt-get install -y \
    openssh-server \
    build-essential \
    supervisor \
    && rm -rf /var/lib/apt/lists/*

# Setup SSH
RUN mkdir /var/run/sshd
RUN echo 'root:password' | chpasswd
RUN sed -i 's/#PermitRootLogin prohibit-password/PermitRootLogin yes/' /etc/ssh/sshd_config
RUN sed -i 's/#PasswordAuthentication yes/PasswordAuthentication yes/' /etc/ssh/sshd_config

# Create user for SSH connections
RUN useradd -m -s /bin/bash sshuser
RUN echo 'sshuser:sshpassword' | chpasswd

# Copy bridge server code
COPY src/ /opt/udp-bridge/
WORKDIR /opt/udp-bridge

# Compile bridge server
RUN gcc -o udp-bridge-server main.c protocol.c client_table.c udp_forwarder.c -pthread

# Copy supervisor config
COPY config/supervisord.conf /etc/supervisor/conf.d/supervisord.conf

# Expose ports
EXPOSE 22 8080

# Start supervisor
CMD ["/usr/bin/supervisord", "-c", "/etc/supervisor/conf.d/supervisord.conf"]
```

#### Задача 5: Docker Compose
```yaml
# server-udp-bridge/docker-compose.yml
version: '3.8'
services:
  udp-bridge-server:
    build: .
    ports:
      - "2222:22"           # SSH
      - "8080:8080"         # Bridge TCP
      - "5060:5060/udp"     # UDP forwarding
    environment:
      - TARGET_UDP_HOST=${TARGET_UDP_HOST:-localhost}
      - TARGET_UDP_PORT=${TARGET_UDP_PORT:-5060}
      - BRIDGE_TCP_PORT=8080
      - CLIENT_TIMEOUT=300
      - MAX_CLIENTS=1000
    volumes:
      - ./logs:/var/log/udp-bridge
    restart: unless-stopped
    networks:
      - udp-bridge-net

networks:
  udp-bridge-net:
    driver: bridge
```

### Низкий приоритет

#### Задача 6: Android протокол интеграция
```c
// ssh-tunnel-android-app/app/src/main/jni/udp_bridge_protocol.h
#ifndef ANDROID_UDP_BRIDGE_PROTOCOL_H
#define ANDROID_UDP_BRIDGE_PROTOCOL_H

// Include same protocol definitions as server
#include "protocol_common.h"

// Android specific client management
typedef struct android_client {
    uint32_t client_id;
    struct sockaddr_in addr;
    time_t last_activity;
    uint32_t packet_count;
    struct android_client* next;
} android_client_t;

// Android bridge functions
int android_bridge_init(int local_port);
void android_bridge_cleanup(void);
android_client_t* find_or_create_client(struct sockaddr_in* addr);
int send_protocol_message(int tcp_socket, message_type_t type, 
                         uint32_t client_id, const void* data, size_t size);

#endif
```

## Риски и митигация

### Высокие риски
1. **Производительность протокола**
   - Риск: Overhead от протокольных заголовков
   - Митигация: Бенчмарки на раннем этапе, оптимизация размера заголовков

2. **Совместимость с существующим кодом**
   - Риск: Поломка существующей функциональности
   - Митигация: Feature toggle, постепенная миграция

### Средние риски
1. **Сложность debugging**
   - Риск: Трудности в отладке протокола
   - Митигация: Обширное логирование, debug tools

2. **Docker развертывание**
   - Риск: Проблемы с сетевой конфигурацией
   - Митигация: Детальная документация, готовые конфиги

## Метрики успеха

### Функциональные метрики
- [ ] Успешное подключение Android к серверу
- [ ] Корректный forwarding UDP пакетов
- [ ] Правильная маршрутизация ответов
- [ ] Автоматическое управление клиентами

### Производительные метрики
- [ ] Latency < 50ms (улучшение на 30% от текущего)
- [ ] Throughput > 1000 pps (packets per second)
- [ ] Memory usage < 100MB для 1000 клиентов
- [ ] CPU usage < 50% при полной нагрузке

### Надежность
- [ ] Uptime > 99.9% при непрерывной работе 24ч
- [ ] Успешное переподключение при обрыве SSH
- [ ] Нет memory leaks при длительной работе
- [ ] Graceful handling сетевых ошибок

Этот план обеспечивает поэтапную реализацию с четкими критериями готовности и метриками успеха.
