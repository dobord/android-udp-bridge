#ifndef UDP2TCP_CLIENT_ADAPTER_H
#define UDP2TCP_CLIENT_ADAPTER_H

#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

// Initialize udp2tcp adapter
// remote_host: udp2tcp server host (may be 127.0.0.1 locally via SSH forward)
// remote_port: udp2tcp server TCP port (forwarded inside SSH)
// local_udp_port: local UDP listen port for applications
int udp2tcp_init(const char* remote_host, int remote_port, int local_udp_port);

// Advanced initialization: allows specifying the remote UDP destination endpoint
// dst_ip (IPv4 string) and dst_port - where the server will send traffic.
// If not specified, 127.0.0.1:local_udp_port is used.
int udp2tcp_init_advanced(const char* remote_host, int remote_port, int local_udp_port,
						  const char* dst_ip, int dst_port);

// Start background handler
int udp2tcp_start(void);

// Stop
int udp2tcp_stop(void);

// Cleanup resources
void udp2tcp_cleanup(void);

// Statistics
void udp2tcp_get_stats(uint64_t* rx_packets, uint64_t* tx_packets, uint64_t* rx_bytes, uint64_t* tx_bytes);

// Retrieve detailed library statistics (if supported by the C API)
int udp2tcp_get_library_stats(uint64_t* tx_frames, uint64_t* rx_frames,
							  uint64_t* tx_bytes, uint64_t* rx_bytes);

// Check if adapter is running
int udp2tcp_is_running(void);

#ifdef __cplusplus
}
#endif

#endif // UDP2TCP_CLIENT_ADAPTER_H
