// Public C API for desktop test CLI to reuse the same native sources as the Android app.
// All comments and logs must be in English (project policy).

#ifndef SSHTUNNEL_API_H
#define SSHTUNNEL_API_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Connect to SSH server with password authentication.
// Returns 0 on success, non-zero on error.
int ssht_cli_connect_password(const char *host, int port, const char *username, const char *password);

// Connect to SSH server with key authentication (passphrase can be NULL).
// Returns 0 on success, non-zero on error.
int ssht_cli_connect_key(
    const char *host, int port, const char *username, const char *private_key_path, const char *passphrase);

// Disconnect current SSH session and stop forwarding.
void ssht_cli_disconnect(void);

// Start local TCP port forward (127.0.0.1:listen_port) to remote_host:remote_port via SSH direct-tcpip.
// Returns 0 on success, non-zero on error.
int ssht_cli_start_port_forward(const char *remote_host, int remote_port, const char *listen_host, int listen_port);

// Start udp2tcp with the same 8-argument contract as the Android JNI call.
// Returns 0 on success, non-zero on error.
int ssht_cli_start_udp2tcp(
    const char *remote_bridge_host,
    int remote_bridge_port,
    const char *local_bridge_host,
    int local_bridge_port,
    const char *local_udp_host,
    int local_udp_port,
    const char *remote_udp_host,
    int remote_udp_port);

// Stop udp2tcp and cleanup.
void ssht_cli_stop_udp2tcp(void);

// Query connection state.
int ssht_cli_is_connected(void);

// Query udp2tcp running state.
int ssht_cli_is_udp2tcp_running(void);

// Retrieve udp2tcp stats; returns 0 on success.
int ssht_cli_get_udp2tcp_stats(uint64_t *tx_frames, uint64_t *rx_frames, uint64_t *tx_bytes, uint64_t *rx_bytes);

#ifdef __cplusplus
}
#endif

#endif // SSHTUNNEL_API_H
