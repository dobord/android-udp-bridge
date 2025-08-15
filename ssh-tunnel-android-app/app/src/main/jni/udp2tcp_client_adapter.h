#ifndef UDP2TCP_CLIENT_ADAPTER_H
#define UDP2TCP_CLIENT_ADAPTER_H

#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

// Start background handler with explicit parameters
// tcp_connect.host: TCP host to connect to (e.g., 127.0.0.1 for SSH forward)
// tcp_connect.port: TCP port to connect to
// listen_addr: local UDP listen address (e.g., 0.0.0.0)
// listen_port: local UDP listen port
// remote_dst_ip: destination UDP IP on the server side
// remote_dst_port: destination UDP port on the server side
int udp2tcp_start(const char* tcp_connect_host,
				  int tcp_connect_port,
				  const char* listen_addr,
				  int listen_port,
				  const char* remote_dst_ip,
				  int remote_dst_port);

// Stop
int udp2tcp_stop(void);

// Cleanup resources
void udp2tcp_cleanup(void);

// Retrieve library statistics (polled from Java layer)
int udp2tcp_get_library_stats(uint64_t* tx_frames, uint64_t* rx_frames,
							  uint64_t* tx_bytes, uint64_t* rx_bytes);

// Check if adapter is running
int udp2tcp_is_running(void);

#ifdef __cplusplus
}
#endif

#endif // UDP2TCP_CLIENT_ADAPTER_H
