#ifndef TCP_UDP_BRIDGE_H
#define TCP_UDP_BRIDGE_H

#include "tcp_connection_manager.h"
#include "protocol_common.h"

#ifndef __ANDROID__
// Mock Android logging for testing
#define LOG_TAG "TcpUdpBridge"
#define LOGD(...) printf(__VA_ARGS__); printf("\n")
#define LOGI(...) printf(__VA_ARGS__); printf("\n")
#define LOGW(...) printf(__VA_ARGS__); printf("\n")
#define LOGE(...) printf(__VA_ARGS__); printf("\n")
#else
#include <android/log.h>
// Logging macros
#define LOG_TAG "TcpUdpBridge"
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#endif

// Forward declarations
typedef struct udp_listener_ctx udp_listener_ctx_t;
typedef struct android_protocol_ctx android_protocol_ctx_t;

// Bridge integration context
typedef struct {
    tcp_connection_manager_t* tcp_manager;
    udp_listener_ctx_t* udp_listener;
    android_protocol_ctx_t* protocol_ctx;
    
    // Threading
    pthread_mutex_t bridge_mutex;
    int active;
    
    // Statistics
    uint64_t udp_to_tcp_packets;
    uint64_t tcp_to_udp_packets;
    uint64_t udp_to_tcp_bytes;
    uint64_t tcp_to_udp_bytes;
    uint32_t bridge_errors;
    
    // Configuration
    char server_host[256];
    int server_port;
    int local_udp_port;
} tcp_udp_bridge_t;

// Bridge management functions
tcp_udp_bridge_t* tcp_udp_bridge_create(void);
void tcp_udp_bridge_destroy(tcp_udp_bridge_t* bridge);

// Configuration
int tcp_udp_bridge_configure(tcp_udp_bridge_t* bridge, 
                            const char* server_host, int server_port, int local_udp_port);

// Connection management
int tcp_udp_bridge_start(tcp_udp_bridge_t* bridge);
void tcp_udp_bridge_stop(tcp_udp_bridge_t* bridge);
int tcp_udp_bridge_is_active(tcp_udp_bridge_t* bridge);

// Data forwarding
int tcp_udp_bridge_forward_udp_to_tcp(tcp_udp_bridge_t* bridge, 
                                      uint32_t client_id, const void* data, size_t size);
int tcp_udp_bridge_forward_tcp_to_udp(tcp_udp_bridge_t* bridge,
                                      uint32_t client_id, const void* data, size_t size);

// Statistics
void tcp_udp_bridge_get_stats(tcp_udp_bridge_t* bridge,
                              uint64_t* udp_to_tcp_packets, uint64_t* tcp_to_udp_packets,
                              uint64_t* udp_to_tcp_bytes, uint64_t* tcp_to_udp_bytes,
                              uint32_t* errors);
void tcp_udp_bridge_reset_stats(tcp_udp_bridge_t* bridge);

// Status information
const char* tcp_udp_bridge_get_status(tcp_udp_bridge_t* bridge);
const char* tcp_udp_bridge_get_last_error(tcp_udp_bridge_t* bridge);

// Integration helpers
int tcp_udp_bridge_set_udp_listener(tcp_udp_bridge_t* bridge, udp_listener_ctx_t* udp_listener);
int tcp_udp_bridge_set_tcp_manager(tcp_udp_bridge_t* bridge, tcp_connection_manager_t* tcp_manager);
android_protocol_ctx_t* tcp_udp_bridge_get_protocol_context(tcp_udp_bridge_t* bridge);

#endif // TCP_UDP_BRIDGE_H
