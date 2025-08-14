#ifndef UDP2TCP_CLIENT_ADAPTER_H
#define UDP2TCP_CLIENT_ADAPTER_H

#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

// Инициализация адаптера udp2tcp
// remote_host: хост udp2tcp сервера (через SSH forward локально может быть 127.0.0.1)
// remote_port: TCP порт udp2tcp сервера (форвард внутри SSH)
// local_udp_port: локальный UDP listen порт для приложений
int udp2tcp_init(const char* remote_host, int remote_port, int local_udp_port);

// Расширенная инициализация: позволяет указать удалённый UDP endpoint назначения
// dst_ip (строка IPv4) и dst_port - куда сервер будет отправлять трафик.
// Если не указано, используется 127.0.0.1:local_udp_port.
int udp2tcp_init_advanced(const char* remote_host, int remote_port, int local_udp_port,
						  const char* dst_ip, int dst_port);

// Запуск фонового обработчика
int udp2tcp_start(void);

// Остановка
int udp2tcp_stop(void);

// Освобождение ресурсов
void udp2tcp_cleanup(void);

// Статистика
void udp2tcp_get_stats(uint64_t* rx_packets, uint64_t* tx_packets, uint64_t* rx_bytes, uint64_t* tx_bytes);

// Получение детализированной статистики из библиотеки (если поддерживается C API)
int udp2tcp_get_library_stats(uint64_t* tx_frames, uint64_t* rx_frames,
							  uint64_t* tx_bytes, uint64_t* rx_bytes);

// Проверка активен ли адаптер
int udp2tcp_is_running(void);

#ifdef __cplusplus
}
#endif

#endif // UDP2TCP_CLIENT_ADAPTER_H
