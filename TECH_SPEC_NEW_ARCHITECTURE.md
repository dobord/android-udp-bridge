# Техническое задание: Новая архитектура UDP-over-SSH

## 1. Обзор архитектуры

### 1.1 Текущая архитектура
В текущей реализации android-udp-bridge использует прямой SSH туннель, где:
- Android приложение создает SSH соединение к серверу
- UDP пакеты напрямую туннелируются через SSH каналы
- Каждый UDP пакет создает новый SSH канал

### 1.2 Новая архитектура
Предлагается изменить архитектуру на client-server модель с протоколом для UDP мультиплексирования:

```
[UDP Client] ←→ [Android UDP Bridge] ←→ [SSH TCP Tunnel] ←→ [Server UDP Bridge] ←→ [Target UDP Server]
```

## 2. Компоненты системы

### 2.1 Android UDP Bridge (Клиентская часть)

#### 2.1.1 UDP Listener
**Функции:**
- Прослушивание UDP портов для входящих пакетов от клиентов
- Идентификация клиентов по адресу и порту отправителя
- Присвоение уникального `client_id` для каждого клиента

**Структура данных клиента:**
```c
typedef struct {
    uint32_t client_id;         // Уникальный ID клиента
    struct sockaddr_in addr;    // IP адрес и порт клиента
    time_t last_activity;       // Время последней активности
    uint32_t packet_count;      // Счетчик пакетов
} udp_client_t;
```

**Алгоритм работы:**
1. Принять UDP пакет от клиента
2. Извлечь адрес отправителя (IP:Port)
3. Найти или создать запись клиента в таблице
4. Присвоить/получить `client_id`
5. Упаковать данные в протокольное сообщение
6. Отправить через TCP туннель

#### 2.1.2 Protocol Handler
**Протокол сообщений:**
```c
typedef struct {
    uint8_t  magic[4];          // "UDPB" - магические байты
    uint8_t  version;           // Версия протокола (1)
    uint8_t  message_type;      // Тип сообщения
    uint16_t flags;             // Флаги
    uint32_t client_id;         // ID клиента
    uint32_t payload_size;      // Размер полезной нагрузки
    uint32_t checksum;          // CRC32 заголовка
} __attribute__((packed)) udp_bridge_header_t;
```

**Типы сообщений:**
- `MSG_DATA` (0x01) - данные UDP пакета
- `MSG_CLIENT_REGISTER` (0x02) - регистрация нового клиента
- `MSG_CLIENT_TIMEOUT` (0x03) - таймаут клиента
- `MSG_PING` (0x04) - проверка соединения
- `MSG_PONG` (0x05) - ответ на ping

#### 2.1.3 TCP Connection Manager
**Функции:**
- Управление SSH TCP соединением
- Отправка протокольных сообщений серверу
- Получение ответов и маршрутизация обратно к UDP клиентам
- Переподключение при обрыве связи

### 2.2 Server UDP Bridge (Серверная часть)

#### 2.2.1 Docker Environment
**Структура контейнера:**
```yaml
# docker-compose.yml
version: '3.8'
services:
  udp-bridge-server:
    build: .
    ports:
      - "22:22"           # SSH порт
      - "8080:8080"       # TCP туннель порт
      - "9999:9999/udp"   # UDP форвардинг порт (проброшен наружу)
    environment:
      - TARGET_UDP_HOST=target-server.example.com
      - TARGET_UDP_PORT=5060
      - BRIDGE_TCP_PORT=8080
    volumes:
      - ./ssh_keys:/etc/ssh/keys:ro
```

#### 2.2.2 TCP Protocol Handler
**Функции:**
- Прослушивание TCP подключений от Android клиентов
- Парсинг протокольных сообщений
- Управление таблицей активных клиентов
- Маршрутизация UDP пакетов

**Структура сервера:**
```c
typedef struct {
    int tcp_socket;             // TCP сокет для Android клиентов
    int udp_socket;             // UDP сокет для целевого сервера
    struct sockaddr_in target;  // Адрес целевого UDP сервера
    pthread_t tcp_thread;       // Поток обработки TCP
    pthread_t udp_thread;       // Поток обработки UDP ответов
    client_table_t* clients;    // Таблица клиентов
} udp_bridge_server_t;
```

#### 2.2.3 UDP Forwarder
**Функции:**
- Конвертация протокольных сообщений в UDP пакеты
- Отправка UDP пакетов на целевой сервер
- Получение ответов от целевого сервера
- Сопоставление ответов с клиентами и отправка обратно

## 3. Протокол взаимодействия

### 3.1 Регистрация клиента
```
Android → Server: MSG_CLIENT_REGISTER
  client_id: 0 (новый клиент)
  payload: client_address_info

Server → Android: MSG_CLIENT_REGISTER
  client_id: [assigned_id]
  payload: success/error
```

### 3.2 Передача данных
```
Android → Server: MSG_DATA
  client_id: [assigned_id]
  payload: [UDP packet data]

Server → Target: UDP packet to target_host:target_port

Target → Server: UDP response

Server → Android: MSG_DATA
  client_id: [assigned_id]
  payload: [UDP response data]

Android → Client: UDP response to original client
```

### 3.3 Управление таймаутами
```
Android → Server: MSG_CLIENT_TIMEOUT
  client_id: [expired_id]
  payload: empty

Server: Cleanup client entry
```

## 4. Конфигурация

### 4.1 Android приложение
```java
public class UdpBridgeConfig {
    // SSH настройки
    private String sshHost;
    private int sshPort = 22;
    private String sshUsername;
    private String sshPassword;
    private String sshPrivateKey;
    
    // Bridge настройки
    private int localUdpPort = 5060;        // Локальный UDP порт
    private int bridgeTcpPort = 8080;       // TCP порт на сервере
    private int clientTimeout = 300;        // Таймаут клиента (сек)
    private int maxClients = 1000;          // Максимум клиентов
    
    // Целевой сервер (настраивается на server side)
    // private String targetHost;  // Не нужно в Android
    // private int targetPort;     // Настраивается на сервере
}
```

### 4.2 Server Bridge
```bash
# Environment variables
UDP_BRIDGE_TCP_PORT=8080
TARGET_UDP_HOST=sip-server.example.com
TARGET_UDP_PORT=5060
CLIENT_TIMEOUT=300
MAX_CLIENTS=1000
LOG_LEVEL=INFO
```

### 4.3 Пользовательский интерфейс Android приложения

#### 4.3.1 Главный экран
**Структура интерфейса:**
- **Список подключений** - основная область экрана
- **Кнопка "+"** - в верхней части экрана для добавления новой настройки
- **Переключатель ON/OFF** - справа от каждого элемента списка

#### 4.3.2 Элемент списка подключений
**Отображаемая информация:**
```
┌─────────────────────────────────────────┬──────┐
│ [Название подключения]                  │ [ON] │
│ ssh://user@server.com:22 → 192.168.1.1 │ OFF  │
│ UDP: 5060 → 5060                        │      │
└─────────────────────────────────────────┴──────┘
```

**Элементы:**
- **Название подключения** - пользовательское имя конфигурации
- **SSH строка подключения** - формат "ssh://user@host:port"
- **Целевой сервер** - IP адрес или домен целевого UDP сервера
- **UDP порты** - локальный порт → удаленный порт
- **Переключатель** - включение/выключение туннеля

#### 4.3.3 Действия пользователя
**Основные действия:**
- **Клик по кнопке "+"** → Открытие экрана создания новой настройки
- **Клик по элементу списка** → Открытие экрана редактирования настройки
- **Переключатель ON/OFF** → Включение/выключение туннеля
- **Долгий клик** → Контекстное меню (удалить, дублировать, экспорт)

#### 4.3.4 Экран настройки подключения
**Поля конфигурации:**
```
┌─────────────────────────────────────────┐
│ Название подключения                    │
│ [Офисный SIP сервер                  ]  │
│                                         │
│ SSH подключение                         │
│ Хост: [server.company.com            ]  │
│ Порт: [22                            ]  │
│ Пользователь: [username              ]  │
│ Пароль: [••••••••                   ]  │
│ ☐ Использовать SSH ключ                │
│                                         │
│ UDP настройки                           │
│ Локальный порт: [5060                ]  │
│ Удаленный хост: [192.168.1.100       ]  │
│ Удаленный порт: [5060                ]  │
│                                         │
│ Дополнительные настройки                │
│ Таймаут клиента: [300] сек              │
│ Максимум клиентов: [1000             ]  │
│                                         │
│ [ Сохранить ] [ Тест подключения ]      │
└─────────────────────────────────────────┘
```

#### 4.3.5 Индикаторы состояния
**Визуальные индикаторы:**
- **Зеленый** - туннель активен и работает
- **Красный** - ошибка подключения
- **Желтый** - подключение в процессе
- **Серый** - туннель выключен

**Дополнительная информация:**
- Количество активных клиентов
- Время работы туннеля
- Статистика пакетов (отправлено/получено)

## 5. Реализация

### 5.1 Этапы разработки

#### Этап 1: Server UDP Bridge
1. Создать Docker образ с SSH сервером
2. Реализовать TCP протокол handler
3. Реализовать UDP forwarder
4. Настроить Docker Compose конфигурацию

#### Этап 2: Android UDP Bridge (модификация)
1. Модифицировать существующий UDP listener
2. Добавить протокол для TCP сообщений
3. Реализовать client management
4. Интегрировать с существующим SSH кодом

#### Этап 3: Тестирование и оптимизация
1. Unit тесты для протокола
2. Integration тесты end-to-end
3. Performance тесты
4. Стресс тесты с множественными клиентами

### 5.2 Структура файлов

```
server-udp-bridge/
├── Dockerfile
├── docker-compose.yml
├── src/
│   ├── main.c
│   ├── protocol.h
│   ├── protocol.c
│   ├── client_table.h
│   ├── client_table.c
│   ├── udp_forwarder.h
│   └── udp_forwarder.c
├── config/
│   ├── sshd_config
│   └── supervisord.conf
└── scripts/
    ├── entrypoint.sh
    └── setup_ssh.sh

ssh-tunnel-android-app/
├── app/src/main/jni/
│   ├── udp_bridge_protocol.h      # Новый
│   ├── udp_bridge_protocol.c      # Новый
│   ├── client_manager.h           # Новый
│   ├── client_manager.c           # Новый
│   └── ssh_tunnel_bridge.c        # Модифицированный
└── app/src/main/java/
    └── com/example/sshtunnel/
        ├── UdpBridgeConfig.java   # Новый
        └── UdpBridgeService.java  # Модифицированный
```

## 6. Преимущества новой архитектуры

### 6.1 Производительность

### 6.2 Масштабируемость

### 6.3 Надежность

### 6.4 Гибкость

## 7. Совместимость и миграция

### 7.1 Обратная совместимость

### 7.2 План миграции
1. Развертывание server bridge в тестовой среде
2. Добавление переключателя в Android приложение
3. Тестирование с реальными пользователями
4. Постепенный переход на новую архитектуру
5. Удаление старого кода после стабилизации

## 8. Мониторинг и метрики
### 8.1 Server metrics
- Количество активных клиентов
- Throughput UDP пакетов
- Latency обработки
- Ошибки протокола

### 8.2 Android metrics
- Время подключения
- Packet loss rate
- Reconnection frequency
- Memory usage

Это техническое задание обеспечивает основу для реализации новой эффективной архитектуры UDP-over-SSH с улучшенной производительностью и масштабируемостью.
