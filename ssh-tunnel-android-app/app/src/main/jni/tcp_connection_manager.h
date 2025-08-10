#ifndef TCP_CONNECTION_MANAGER_H
#define TCP_CONNECTION_MANAGER_H

#include "protocol_common.h"
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>

#ifndef __ANDROID__
// Mock Android logging for testing
#define TCP_LOG_TAG "TcpConnectionManager"
#define TCP_LOGD(...) printf(__VA_ARGS__); printf("\n")
#define TCP_LOGI(...) printf(__VA_ARGS__); printf("\n")
#define TCP_LOGW(...) printf(__VA_ARGS__); printf("\n")
#define TCP_LOGE(...) printf(__VA_ARGS__); printf("\n")
#else
#include <android/log.h>
// Logging macros
#define TCP_LOG_TAG "TcpConnectionManager"
#define TCP_LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, TCP_LOG_TAG, __VA_ARGS__)
#define TCP_LOGI(...) __android_log_print(ANDROID_LOG_INFO, TCP_LOG_TAG, __VA_ARGS__)
#define TCP_LOGW(...) __android_log_print(ANDROID_LOG_WARN, TCP_LOG_TAG, __VA_ARGS__)
#define TCP_LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TCP_LOG_TAG, __VA_ARGS__)
#endif

// Forward declarations
typedef struct tcp_protocol_ctx tcp_protocol_ctx_t;

// Connection states
typedef enum {
    TCP_CONN_DISCONNECTED = 0,
    TCP_CONN_CONNECTING = 1,
    TCP_CONN_CONNECTED = 2,
    TCP_CONN_RECONNECTING = 3,
    TCP_CONN_ERROR = 4
} tcp_connection_state_t;

// Connection statistics
typedef struct {
    uint64_t bytes_sent;
    uint64_t bytes_received;
    uint32_t messages_sent;
    uint32_t messages_received;
    uint32_t reconnect_count;
    time_t last_activity;
    time_t connection_start_time;
} tcp_connection_stats_t;

// Reconnection configuration
typedef struct {
    int enabled;                    // Enable auto-reconnection
    int max_attempts;              // Maximum reconnection attempts (-1 for unlimited)
    int initial_delay_ms;          // Initial delay between attempts
    int max_delay_ms;              // Maximum delay between attempts
    float backoff_multiplier;      // Exponential backoff multiplier
    int jitter_ms;                 // Random jitter to add to delays
} tcp_reconnect_config_t;

// TCP Connection Manager context
typedef struct {
    // Connection details
    char server_host[256];
    int server_port;
    int socket_fd;
    tcp_connection_state_t state;
    
    // Protocol integration
    tcp_protocol_ctx_t* protocol_ctx;
    
    // Threading
    pthread_t receiver_thread;
    pthread_t reconnect_thread;
    pthread_mutex_t state_mutex;
    pthread_cond_t state_cond;
    int shutdown_flag;
    
    // Statistics and monitoring
    tcp_connection_stats_t stats;
    pthread_mutex_t stats_mutex;
    
    // Reconnection management
    tcp_reconnect_config_t reconnect_config;
    int current_reconnect_attempt;
    int current_delay_ms;
    
    // Response handling
    int response_timeout_ms;
    pthread_mutex_t response_mutex;
    pthread_cond_t response_cond;
    
    // Error handling
    char last_error[256];
    time_t last_error_time;
} tcp_connection_manager_t;

// Core connection management functions
tcp_connection_manager_t* tcp_connection_manager_create(void);
void tcp_connection_manager_destroy(tcp_connection_manager_t* manager);

// Connection operations
int tcp_connection_manager_connect(tcp_connection_manager_t* manager, 
                                  const char* server_host, int server_port);
void tcp_connection_manager_disconnect(tcp_connection_manager_t* manager);
int tcp_connection_manager_is_connected(tcp_connection_manager_t* manager);
tcp_connection_state_t tcp_connection_manager_get_state(tcp_connection_manager_t* manager);

// Protocol message handling
int tcp_connection_manager_send_message(tcp_connection_manager_t* manager,
                                       message_type_t type, uint32_t client_id,
                                       const void* payload, uint32_t payload_size);
int tcp_connection_manager_send_data(tcp_connection_manager_t* manager,
                                    uint32_t client_id, const void* data, size_t size);
int tcp_connection_manager_send_ping(tcp_connection_manager_t* manager);
int tcp_connection_manager_register_client(tcp_connection_manager_t* manager,
                                          uint32_t client_id, struct sockaddr_in* client_addr);

// Reconnection management
void tcp_connection_manager_configure_reconnect(tcp_connection_manager_t* manager,
                                               const tcp_reconnect_config_t* config);
int tcp_connection_manager_enable_reconnect(tcp_connection_manager_t* manager, int enabled);
void tcp_connection_manager_trigger_reconnect(tcp_connection_manager_t* manager);

// Statistics and monitoring
tcp_connection_stats_t tcp_connection_manager_get_stats(tcp_connection_manager_t* manager);
void tcp_connection_manager_reset_stats(tcp_connection_manager_t* manager);
const char* tcp_connection_manager_get_last_error(tcp_connection_manager_t* manager);

// Protocol context integration
void tcp_connection_manager_set_protocol_context(tcp_connection_manager_t* manager,
                                                 tcp_protocol_ctx_t* protocol_ctx);

// Response handling
int tcp_connection_manager_wait_for_response(tcp_connection_manager_t* manager,
                                            message_type_t expected_type, int timeout_ms);

// Helper functions
const char* tcp_connection_state_string(tcp_connection_state_t state);
int tcp_connection_manager_set_socket_options(int socket_fd);

// Internal thread functions
void* tcp_receiver_thread(void* arg);
void* tcp_reconnect_thread(void* arg);

// Internal helper functions
int tcp_connection_manager_create_socket(tcp_connection_manager_t* manager);
int tcp_connection_manager_handle_received_data(tcp_connection_manager_t* manager,
                                               const char* data, size_t size);
void tcp_connection_manager_update_stats_sent(tcp_connection_manager_t* manager,
                                              size_t bytes, int is_message);
void tcp_connection_manager_update_stats_received(tcp_connection_manager_t* manager,
                                                  size_t bytes, int is_message);
void tcp_connection_manager_set_error(tcp_connection_manager_t* manager, const char* error);
int tcp_connection_manager_calculate_reconnect_delay(tcp_connection_manager_t* manager);

#endif // TCP_CONNECTION_MANAGER_H
