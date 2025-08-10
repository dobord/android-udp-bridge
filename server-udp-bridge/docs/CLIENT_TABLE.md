# Client Table Module

Модуль управления клиентами для UDP Bridge Server. Обеспечивает эффективное управление множественными клиентскими соединениями с автоматическим управлением жизненным циклом и статистикой.

## Возможности

- **Управление клиентами**: Добавление, поиск, удаление клиентов с уникальными ID
- **Автоматические таймауты**: Фоновая очистка неактивных клиентов
- **Статистика**: Отслеживание байтов, пакетов и времени активности
- **Потокобезопасность**: Использование mutex для безопасного доступа из нескольких потоков
- **Масштабируемость**: Поддержка тысяч одновременных клиентов

## API

### Основные функции

```c
// Создание таблицы клиентов
client_table_t* client_table_create(uint32_t max_clients, time_t timeout_seconds);

// Уничтожение таблицы
void client_table_destroy(client_table_t* table);

// Добавление клиента
uint32_t client_table_add(client_table_t* table, int tcp_socket);

// Поиск клиента
client_entry_t* client_table_find(client_table_t* table, uint32_t client_id);

// Удаление клиента
int client_table_remove(client_table_t* table, uint32_t client_id);
```

### Управление активностью

```c
// Обновление времени активности
void client_table_update_activity(client_table_t* table, uint32_t client_id);

// Обновление статистики
void client_table_update_stats(client_table_t* table, uint32_t client_id, 
                              uint64_t bytes_received, uint64_t bytes_sent);
```

### Автоматическая очистка

```c
// Запуск фонового потока очистки
int client_table_start_cleanup(client_table_t* table);

// Остановка фонового потока
void client_table_stop_cleanup(client_table_t* table);

// Ручная очистка просроченных клиентов
int client_table_cleanup_expired(client_table_t* table);
```

### Мониторинг

```c
// Получение количества активных клиентов
uint32_t client_table_get_count(client_table_t* table);

// Печать статистики (для отладки)
void client_table_print_stats(client_table_t* table);
```

## Структуры данных

### client_entry_t
```c
typedef struct client_entry {
    uint32_t client_id;                    // Уникальный идентификатор клиента
    time_t last_activity;                  // Время последней активности
    uint64_t bytes_received;               // Всего байт получено от клиента
    uint64_t bytes_sent;                   // Всего байт отправлено клиенту
    uint32_t packet_count;                 // Общее количество пакетов
    int tcp_socket;                        // TCP сокет для связи с Android
    struct client_entry* next;             // Следующий элемент в списке
} client_entry_t;
```

### client_table_t
```c
typedef struct {
    client_entry_t* head;                  // Голова связанного списка
    uint32_t count;                        // Количество активных клиентов
    uint32_t next_id;                      // Следующий доступный ID
    uint32_t max_clients;                  // Максимальное количество клиентов
    time_t timeout_seconds;                // Таймаут клиента в секундах
    pthread_mutex_t mutex;                 // Мьютекс для потокобезопасности
    pthread_t cleanup_thread;              // Фоновый поток очистки
    int cleanup_running;                   // Флаг работы потока очистки
} client_table_t;
```

## Примеры использования

### Базовое использование

```c
#include "client_table.h"

// Создание таблицы на 1000 клиентов с таймаутом 300 секунд
client_table_t* table = client_table_create(1000, 300);
if (!table) {
    fprintf(stderr, "Failed to create client table\\n");
    return -1;
}

// Добавление клиента
int client_socket = accept(server_socket, ...);
uint32_t client_id = client_table_add(table, client_socket);
if (client_id == 0) {
    fprintf(stderr, "Failed to add client\\n");
    close(client_socket);
} else {
    printf("Added client with ID %u\\n", client_id);
}

// Обновление активности при получении данных
client_table_update_activity(table, client_id);
client_table_update_stats(table, client_id, received_bytes, sent_bytes);

// Очистка
client_table_destroy(table);
```

### Использование с автоматической очисткой

```c
// Создание таблицы
client_table_t* table = client_table_create(1000, 300);

// Запуск фонового потока очистки
if (client_table_start_cleanup(table) != 0) {
    fprintf(stderr, "Failed to start cleanup thread\\n");
    client_table_destroy(table);
    return -1;
}

// Использование таблицы...
// Фоновый поток автоматически удалит неактивных клиентов

// Очистка (автоматически остановит поток очистки)
client_table_destroy(table);
```

### Мониторинг статистики

```c
// Печать текущей статистики
client_table_print_stats(table);

// Получение количества активных клиентов
uint32_t active_clients = client_table_get_count(table);
printf("Active clients: %u\\n", active_clients);

// Поиск конкретного клиента
client_entry_t* client = client_table_find(table, client_id);
if (client) {
    printf("Client %u: RX=%lu, TX=%lu, packets=%u\\n",
           client->client_id, client->bytes_received, 
           client->bytes_sent, client->packet_count);
}
```

## Сборка и тестирование

### Сборка

```bash
# Сборка всех компонентов
make all

# Сборка только тестов client_table
make client-table-test

# Сборка демо
make demo
```

### Тестирование

```bash
# Запуск всех тестов
make test

# Запуск только тестов client_table
make client-table-test

# Запуск интеграционного демо
make demo
```

## Производительность

### Характеристики

- **Время поиска**: O(n) в худшем случае, O(1) в среднем при небольшом количестве клиентов
- **Память**: ~64 байта на клиента + overhead системы
- **Масштабируемость**: Протестировано на 1000+ одновременных клиентов
- **Потокобезопасность**: Полная с использованием pthread_mutex

### Оптимизации

- Связанный список для быстрого добавления/удаления
- Фоновый поток очистки для минимизации блокировок
- Эффективное управление памятью
- Минимальные системные вызовы

## Интеграция

Модуль спроектирован для интеграции с:

- **UDP Forwarder**: Отслеживание клиентов для маршрутизации пакетов
- **Protocol Handler**: Валидация client_id в протокольных сообщениях  
- **Main Server**: Управление TCP соединениями
- **Logging System**: Статистика и мониторинг

## Безопасность

- Валидация всех входных параметров
- Защита от переполнения буферов
- Безопасное управление памятью
- Graceful handling сетевых ошибок

## Диагностика

### Логи

Модуль выводит подробные логи:
- Добавление/удаление клиентов
- Статистика очистки
- Ошибки и предупреждения

### Отладка

Функция `client_table_print_stats()` предоставляет:
- Количество активных клиентов
- Статистику по каждому клиенту
- Информацию о времени неактивности
- Состояние фонового потока

## Совместимость

- **Платформы**: Linux, macOS (с pthread)
- **Компиляторы**: GCC, Clang
- **Стандарт**: C99
- **Зависимости**: pthread, стандартная библиотека C
