# Схема проброса UDP порта - Новая архитектура

## Общая схема архитектуры

```
┌─────────────────┐    UDP     ┌─────────────────────┐    TCP/SSH    ┌─────────────────────┐    UDP     ┌─────────────────┐
│   UDP Client    │◄──────────►│  Android UDP Bridge │◄─────────────►│  Server UDP Bridge  │◄──────────►│  Target Server  │
│                 │            │                     │               │   (Docker)          │            │                 │
│ 192.168.1.100   │            │ Android App         │               │                     │            │ sip.example.com │
│ Port: Dynamic   │            │ Port: 5060          │               │ SSH: 22            │            │ Port: 5060      │
└─────────────────┘            └─────────────────────┘               │ Bridge: 8080        │            └─────────────────┘
                                                                     └─────────────────────┘
```

## Детальная схема протокола

### 1. Инициализация соединения

```
┌─────────────┐                 ┌─────────────┐                 ┌─────────────┐
│   Android   │                 │     SSH     │                 │   Server    │
│   Bridge    │                 │   Tunnel    │                 │   Bridge    │
└─────────────┘                 └─────────────┘                 └─────────────┘
       │                               │                               │
       │        SSH Connection         │                               │
       │◄─────────────────────────────►│                               │
       │                               │                               │
       │      TCP Tunnel (port 8080)   │                               │
       │◄──────────────────────────────┼──────────────────────────────►│
       │                               │                               │
```

### 2. Регистрация UDP клиента

```
┌─────────────┐    ┌─────────────┐    ┌─────────────┐    ┌─────────────┐    ┌─────────────┐
│ UDP Client  │    │   Android   │    │     SSH     │    │   Server    │    │   Target    │
│             │    │   Bridge    │    │   Tunnel    │    │   Bridge    │    │   Server    │
└─────────────┘    └─────────────┘    └─────────────┘    └─────────────┘    └─────────────┘
       │                   │                   │                   │                   │
   (1) │ UDP Packet        │                   │                   │                   │
       │──────────────────►│                   │                   │                   │
       │                   │                   │                   │                   │
   (2) │                   │ Client Registration                   │                   │
       │                   │ MSG_CLIENT_REGISTER                   │                   │
       │                   │ client_id: 0      │                   │                   │
       │                   │◄─────────────────►│◄─────────────────►│                   │
       │                   │                   │                   │                   │
   (3) │                   │ Response          │                   │                   │
       │                   │ client_id: 1001   │                   │                   │
       │                   │◄─────────────────►│◄─────────────────►│                   │
       │                   │                   │                   │                   │
```

### 3. Передача UDP данных

```
┌─────────────┐    ┌─────────────┐    ┌─────────────┐    ┌─────────────┐    ┌─────────────┐
│ UDP Client  │    │   Android   │    │     SSH     │    │   Server    │    │   Target    │
│             │    │   Bridge    │    │   Tunnel    │    │   Bridge    │    │   Server    │
└─────────────┘    └─────────────┘    └─────────────┘    └─────────────┘    └─────────────┘
       │                   │                   │                   │                   │
   (1) │ UDP Packet        │                   │                   │                   │
       │ (SIP INVITE)      │                   │                   │                   │
       │──────────────────►│                   │                   │                   │
       │                   │                   │                   │                   │
   (2) │                   │ MSG_DATA          │                   │                   │
       │                   │ client_id: 1001   │                   │                   │
       │                   │ payload: SIP data │                   │                   │
       │                   │◄─────────────────►│◄─────────────────►│                   │
       │                   │                   │                   │                   │
   (3) │                   │                   │                   │ UDP Forward       │
       │                   │                   │                   │ To target:5060    │
       │                   │                   │                   │──────────────────►│
       │                   │                   │                   │                   │
   (4) │                   │                   │                   │ UDP Response      │
       │                   │                   │                   │ (SIP 200 OK)      │
       │                   │                   │                   │◄──────────────────│
       │                   │                   │                   │                   │
   (5) │                   │ MSG_DATA          │                   │                   │
       │                   │ client_id: 1001   │                   │                   │
       │                   │ payload: SIP OK   │                   │                   │
       │                   │◄─────────────────►│◄─────────────────►│                   │
       │                   │                   │                   │                   │
   (6) │ UDP Response      │                   │                   │                   │
       │ (SIP 200 OK)      │                   │                   │                   │
       │◄──────────────────│                   │                   │                   │
       │                   │                   │                   │                   │
```

## Протокол сообщений

### Структура заголовка

```
 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|    Magic "UDPB"                              |Ver|Msg|  Flags  |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                        Client ID                              |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                      Payload Size                            |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                      Checksum (CRC32)                        |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                      Payload Data                            |
|                          ...                                 |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```

### Типы сообщений

| Type | Name | Description |
|------|------|-------------|
| 0x01 | MSG_DATA | UDP данные для передачи |
| 0x02 | MSG_CLIENT_REGISTER | Регистрация нового клиента |
| 0x03 | MSG_CLIENT_TIMEOUT | Уведомление о таймауте клиента |
| 0x04 | MSG_PING | Проверка соединения |
| 0x05 | MSG_PONG | Ответ на ping |

## Таблица клиентов на Android Bridge

```c
typedef struct udp_client {
    uint32_t client_id;                    // Уникальный ID (1001, 1002, ...)
    struct sockaddr_in client_addr;       // IP:Port клиента
    time_t last_activity;                 // Время последней активности
    uint32_t packet_count;                // Счетчик пакетов
    struct udp_client* next;              // Связанный список
} udp_client_t;
```

**Пример таблицы:**
```
Client ID | Client Address    | Last Activity | Packets
----------|-------------------|---------------|--------
1001      | 192.168.1.100:5432| 1692547890   | 15
1002      | 192.168.1.101:6543| 1692547885   | 8
1003      | 10.0.0.50:7654    | 1692547892   | 3
```

## Таблица клиентов на Server Bridge

```c
typedef struct server_client {
    uint32_t client_id;                    // ID из Android Bridge
    time_t last_activity;                 // Последняя активность
    uint64_t bytes_forwarded;             // Статистика
    struct server_client* next;           // Связанный список
} server_client_t;
```

## Конфигурация Docker

### docker-compose.yml
```yaml
version: '3.8'
services:
  udp-bridge-server:
    build: 
      context: .
      dockerfile: Dockerfile
    ports:
      - "2222:22"           # SSH (можно изменить внешний порт)
      - "8080:8080"         # TCP Bridge внутренний порт
      - "5060:5060/udp"     # UDP форвардинг наружу
    environment:
      - TARGET_UDP_HOST=sip-server.example.com  # Целевой сервер
      - TARGET_UDP_PORT=5060                    # Целевой порт
      - BRIDGE_TCP_PORT=8080                    # Внутренний TCP порт
      - CLIENT_TIMEOUT=300                      # Таймаут клиентов (сек)
      - MAX_CLIENTS=1000                        # Максимум клиентов
      - SSH_USER=sshuser                        # SSH пользователь
      - SSH_PASSWORD=sshpassword                # SSH пароль (или ключ)
    volumes:
      - ./ssh_keys:/home/sshuser/.ssh:ro        # SSH ключи
      - ./logs:/var/log/udp-bridge              # Логи
    restart: unless-stopped
    
  # Опционально: целевой SIP сервер для тестирования
  test-sip-server:
    image: drachtio/drachtio-server:latest
    ports:
      - "5060:5060/udp"
    command: drachtio --contact sip:*:5060 --loglevel debug
```

## Последовательность развертывания

### 1. Подготовка сервера
```bash
# Клонировать репозиторий
git clone https://github.com/your-repo/android-udp-bridge
cd android-udp-bridge

# Создать server bridge
mkdir server-udp-bridge
cd server-udp-bridge

# Настроить конфигурацию
cp config/docker-compose.example.yml docker-compose.yml
# Отредактировать TARGET_UDP_HOST и другие параметры

# Запустить сервер
docker-compose up -d
```

### 2. Настройка Android приложения
```java
UdpBridgeConfig config = new UdpBridgeConfig();
config.setSshHost("your-server.com");
config.setSshPort(2222);
config.setSshUsername("sshuser");
config.setSshPassword("sshpassword");
config.setLocalUdpPort(5060);      // Локальный порт для клиентов
config.setBridgeTcpPort(8080);     // TCP порт на сервере
config.setClientTimeout(300);      // Таймаут клиентов
```

### 3. Тестирование
```bash
# Отправить тестовый UDP пакет
echo "TEST UDP PACKET" | nc -u android-device-ip 5060

# Проверить логи сервера
docker-compose logs -f udp-bridge-server

# Проверить логи Android
adb logcat | grep -E "(UDPBridge|SSHTunnel)"
```

Эта схема обеспечивает:
- **Эффективную мультиплексацию** множественных UDP клиентов
- **Единое TCP соединение** вместо множества SSH каналов
- **Гибкую конфигурацию** целевого сервера
- **Простое развертывание** через Docker
- **Автоматическое управление клиентами** с таймаутами
