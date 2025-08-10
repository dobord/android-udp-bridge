#ifndef UDP_LISTENER_H
#define UDP_LISTENER_H

#include "udp_bridge_protocol.h"
#include "client_manager.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <pthread.h>
#include <android/log.h>

// Android logging for UDP listener
#define UDP_LOG_TAG "UdpListener"
#define UDP_LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, UDP_LOG_TAG, __VA_ARGS__)
#define UDP_LOGI(...) __android_log_print(ANDROID_LOG_INFO, UDP_LOG_TAG, __VA_ARGS__)
#define UDP_LOGW(...) __android_log_print(ANDROID_LOG_WARN, UDP_LOG_TAG, __VA_ARGS__)
#define UDP_LOGE(...) __android_log_print(ANDROID_LOG_ERROR, UDP_LOG_TAG, __VA_ARGS__)

// UDP listener configuration
#define UDP_BUFFER_SIZE 65536       // Maximum UDP packet size
#define UDP_LISTENER_TIMEOUT 1000   // Socket timeout in milliseconds

// UDP listener context structure
typedef struct {
    int udp_socket;                 // Local UDP listening socket
    int local_port;                 // Local UDP port to listen on
    pthread_t listener_thread;      // UDP listener thread
    int should_stop;                // Thread stop flag
    pthread_mutex_t stop_mutex;     // Mutex for thread synchronization
    
    // Protocol integration
    android_protocol_ctx_t* protocol_ctx;  // Protocol context
    
    // Statistics
    uint64_t packets_received;      // Total packets received
    uint64_t packets_sent;          // Total packets sent
    uint64_t bytes_received;        // Total bytes received
    uint64_t bytes_sent;            // Total bytes sent
    uint32_t error_count;           // Number of errors
    
    // Connection info
    char bridge_server_host[256];   // Bridge server hostname
    int bridge_server_port;         // Bridge server port
    int bridge_connected;           // Bridge connection status
} udp_listener_ctx_t;

// UDP listener functions
udp_listener_ctx_t* udp_listener_create(int local_port);
void udp_listener_destroy(udp_listener_ctx_t* ctx);

// Configuration functions
int udp_listener_set_bridge_server(udp_listener_ctx_t* ctx, const char* host, int port);
int udp_listener_connect_bridge(udp_listener_ctx_t* ctx);
void udp_listener_disconnect_bridge(udp_listener_ctx_t* ctx);

// Listener control functions
int udp_listener_start(udp_listener_ctx_t* ctx);
void udp_listener_stop(udp_listener_ctx_t* ctx);
int udp_listener_is_running(udp_listener_ctx_t* ctx);

// Packet handling functions
int udp_listener_handle_packet(udp_listener_ctx_t* ctx, const char* data, 
                              size_t size, struct sockaddr_in* from_addr);
int udp_listener_send_to_client(udp_listener_ctx_t* ctx, uint32_t client_id, 
                               const char* data, size_t size);

// Statistics functions
void udp_listener_get_stats(udp_listener_ctx_t* ctx, uint64_t* packets_rx, 
                           uint64_t* packets_tx, uint64_t* bytes_rx, 
                           uint64_t* bytes_tx, uint32_t* errors);
void udp_listener_reset_stats(udp_listener_ctx_t* ctx);

// Internal thread function
void* udp_listener_thread_func(void* arg);

// Client identification and management
uint32_t udp_listener_identify_client(udp_listener_ctx_t* ctx, struct sockaddr_in* addr);
int udp_listener_update_client_activity(udp_listener_ctx_t* ctx, uint32_t client_id);
int udp_listener_cleanup_expired_clients(udp_listener_ctx_t* ctx);

#endif // UDP_LISTENER_H
