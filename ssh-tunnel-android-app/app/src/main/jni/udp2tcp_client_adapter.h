#ifndef UDP2TCP_CLIENT_ADAPTER_H
#define UDP2TCP_CLIENT_ADAPTER_H

#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

// Start background handler with explicit ServerConfig-aligned parameters (8 args)
// remoteBridgeHost/Port: SSH remote side host:port that localBridge will forward to (diagnostic only here)
// localBridgeHost/Port: TCP connect target (typically 127.0.0.1:<localBridgePort> created by SSH forward)
// localUdpHost/Port: local UDP listen address:port
// remoteUdpHost/Port: destination UDP endpoint on the server side
int udp2tcp_start(const char* remoteBridgeHost,
				  int remoteBridgePort,
				  const char* localBridgeHost,
				  int localBridgePort,
				  const char* localUdpHost,
				  int localUdpPort,
				  const char* remoteUdpHost,
				  int remoteUdpPort);

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
