#include "tcp_connection_manager.h"
#include "udp_bridge_protocol.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <fcntl.h>
#include <netdb.h>
#include <time.h>

// Default configuration values
#define DEFAULT_RESPONSE_TIMEOUT_MS 5000
#define DEFAULT_RECONNECT_MAX_ATTEMPTS 10
#define DEFAULT_RECONNECT_INITIAL_DELAY_MS 1000
#define DEFAULT_RECONNECT_MAX_DELAY_MS 30000
#define DEFAULT_RECONNECT_BACKOFF_MULTIPLIER 2.0f
#define DEFAULT_RECONNECT_JITTER_MS 500
#define TCP_BUFFER_SIZE 8192

// Create a new TCP connection manager
tcp_connection_manager_t* tcp_connection_manager_create(void) {
    tcp_connection_manager_t* manager = calloc(1, sizeof(tcp_connection_manager_t));
    if (!manager) {
        TCP_LOGE("Failed to allocate memory for TCP connection manager");
        return NULL;
    }
    
    // Initialize mutex and condition variables
    if (pthread_mutex_init(&manager->state_mutex, NULL) != 0) {
        TCP_LOGE("Failed to initialize state mutex");
        free(manager);
        return NULL;
    }
    
    if (pthread_cond_init(&manager->state_cond, NULL) != 0) {
        TCP_LOGE("Failed to initialize state condition variable");
        pthread_mutex_destroy(&manager->state_mutex);
        free(manager);
        return NULL;
    }
    
    if (pthread_mutex_init(&manager->stats_mutex, NULL) != 0) {
        TCP_LOGE("Failed to initialize stats mutex");
        pthread_cond_destroy(&manager->state_cond);
        pthread_mutex_destroy(&manager->state_mutex);
        free(manager);
        return NULL;
    }
    
    if (pthread_mutex_init(&manager->response_mutex, NULL) != 0) {
        TCP_LOGE("Failed to initialize response mutex");
        pthread_mutex_destroy(&manager->stats_mutex);
        pthread_cond_destroy(&manager->state_cond);
        pthread_mutex_destroy(&manager->state_mutex);
        free(manager);
        return NULL;
    }
    
    if (pthread_cond_init(&manager->response_cond, NULL) != 0) {
        TCP_LOGE("Failed to initialize response condition variable");
        pthread_mutex_destroy(&manager->response_mutex);
        pthread_mutex_destroy(&manager->stats_mutex);
        pthread_cond_destroy(&manager->state_cond);
        pthread_mutex_destroy(&manager->state_mutex);
        free(manager);
        return NULL;
    }
    
    // Initialize default values
    manager->socket_fd = -1;
    manager->state = TCP_CONN_DISCONNECTED;
    manager->response_timeout_ms = DEFAULT_RESPONSE_TIMEOUT_MS;
    manager->shutdown_flag = 0;
    
    // Initialize reconnection configuration
    manager->reconnect_config.enabled = 1;
    manager->reconnect_config.max_attempts = DEFAULT_RECONNECT_MAX_ATTEMPTS;
    manager->reconnect_config.initial_delay_ms = DEFAULT_RECONNECT_INITIAL_DELAY_MS;
    manager->reconnect_config.max_delay_ms = DEFAULT_RECONNECT_MAX_DELAY_MS;
    manager->reconnect_config.backoff_multiplier = DEFAULT_RECONNECT_BACKOFF_MULTIPLIER;
    manager->reconnect_config.jitter_ms = DEFAULT_RECONNECT_JITTER_MS;
    
    TCP_LOGI("TCP connection manager created successfully");
    return manager;
}

// Destroy TCP connection manager
void tcp_connection_manager_destroy(tcp_connection_manager_t* manager) {
    if (!manager) return;
    
    TCP_LOGI("Destroying TCP connection manager");
    
    // Signal shutdown
    pthread_mutex_lock(&manager->state_mutex);
    manager->shutdown_flag = 1;
    pthread_cond_broadcast(&manager->state_cond);
    pthread_mutex_unlock(&manager->state_mutex);
    
    // Disconnect if connected
    tcp_connection_manager_disconnect(manager);
    
    // Wait for threads to finish
    if (manager->receiver_thread) {
        pthread_join(manager->receiver_thread, NULL);
    }
    if (manager->reconnect_thread) {
        pthread_join(manager->reconnect_thread, NULL);
    }
    
    // Cleanup mutex and condition variables
    pthread_cond_destroy(&manager->response_cond);
    pthread_mutex_destroy(&manager->response_mutex);
    pthread_mutex_destroy(&manager->stats_mutex);
    pthread_cond_destroy(&manager->state_cond);
    pthread_mutex_destroy(&manager->state_mutex);
    
    free(manager);
    TCP_LOGI("TCP connection manager destroyed");
}

// Set socket options for optimal performance
int tcp_connection_manager_set_socket_options(int socket_fd) {
    int opt = 1;
    
    // Enable SO_REUSEADDR
    if (setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        TCP_LOGW("Failed to set SO_REUSEADDR: %s", strerror(errno));
    }
    
    // Enable TCP_NODELAY for low latency
    if (setsockopt(socket_fd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt)) < 0) {
        TCP_LOGW("Failed to set TCP_NODELAY: %s", strerror(errno));
    }
    
    // Set socket to non-blocking mode
    int flags = fcntl(socket_fd, F_GETFL, 0);
    if (flags == -1) {
        TCP_LOGE("Failed to get socket flags: %s", strerror(errno));
        return -1;
    }
    
    if (fcntl(socket_fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        TCP_LOGE("Failed to set socket to non-blocking: %s", strerror(errno));
        return -1;
    }
    
    TCP_LOGD("Socket options configured successfully");
    return 0;
}

// Create and configure socket
int tcp_connection_manager_create_socket(tcp_connection_manager_t* manager) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        tcp_connection_manager_set_error(manager, "Failed to create socket");
        TCP_LOGE("Failed to create socket: %s", strerror(errno));
        return -1;
    }
    
    if (tcp_connection_manager_set_socket_options(sock) < 0) {
        close(sock);
        return -1;
    }
    
    return sock;
}

// Connect to server
int tcp_connection_manager_connect(tcp_connection_manager_t* manager, 
                                  const char* server_host, int server_port) {
    if (!manager || !server_host) {
        TCP_LOGE("Invalid parameters for connect");
        return -1;
    }
    
    pthread_mutex_lock(&manager->state_mutex);
    
    if (manager->state == TCP_CONN_CONNECTED || manager->state == TCP_CONN_CONNECTING) {
        TCP_LOGW("Already connected or connecting");
        pthread_mutex_unlock(&manager->state_mutex);
        return 0;
    }
    
    // Store connection details
    strncpy(manager->server_host, server_host, sizeof(manager->server_host) - 1);
    manager->server_host[sizeof(manager->server_host) - 1] = '\0';
    manager->server_port = server_port;
    
    // Update state
    manager->state = TCP_CONN_CONNECTING;
    manager->current_reconnect_attempt = 0;
    manager->current_delay_ms = manager->reconnect_config.initial_delay_ms;
    
    pthread_mutex_unlock(&manager->state_mutex);
    
    TCP_LOGI("Connecting to %s:%d", server_host, server_port);
    
    // Create socket
    int sock = tcp_connection_manager_create_socket(manager);
    if (sock < 0) {
        pthread_mutex_lock(&manager->state_mutex);
        manager->state = TCP_CONN_ERROR;
        pthread_mutex_unlock(&manager->state_mutex);
        return -1;
    }
    
    // Resolve hostname
    struct hostent* host_entry = gethostbyname(server_host);
    if (!host_entry) {
        tcp_connection_manager_set_error(manager, "Failed to resolve hostname");
        TCP_LOGE("Failed to resolve hostname: %s", server_host);
        close(sock);
        pthread_mutex_lock(&manager->state_mutex);
        manager->state = TCP_CONN_ERROR;
        pthread_mutex_unlock(&manager->state_mutex);
        return -1;
    }
    
    // Setup server address
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(server_port);
    memcpy(&server_addr.sin_addr, host_entry->h_addr_list[0], host_entry->h_length);
    
    // Attempt connection (non-blocking)
    int result = connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr));
    if (result < 0 && errno != EINPROGRESS) {
        tcp_connection_manager_set_error(manager, "Connection failed");
        TCP_LOGE("Connection failed: %s", strerror(errno));
        close(sock);
        pthread_mutex_lock(&manager->state_mutex);
        manager->state = TCP_CONN_ERROR;
        pthread_mutex_unlock(&manager->state_mutex);
        return -1;
    }
    
    // Wait for connection to complete (with timeout)
    fd_set write_fds, error_fds;
    struct timeval timeout = {5, 0}; // 5 seconds timeout
    
    FD_ZERO(&write_fds);
    FD_ZERO(&error_fds);
    FD_SET(sock, &write_fds);
    FD_SET(sock, &error_fds);
    
    int select_result = select(sock + 1, NULL, &write_fds, &error_fds, &timeout);
    if (select_result <= 0) {
        tcp_connection_manager_set_error(manager, "Connection timeout or error");
        TCP_LOGE("Connection timeout or select error: %s", strerror(errno));
        close(sock);
        pthread_mutex_lock(&manager->state_mutex);
        manager->state = TCP_CONN_ERROR;
        pthread_mutex_unlock(&manager->state_mutex);
        return -1;
    }
    
    // Check if connection succeeded
    int sock_error;
    socklen_t len = sizeof(sock_error);
    if (getsockopt(sock, SOL_SOCKET, SO_ERROR, &sock_error, &len) < 0 || sock_error != 0) {
        tcp_connection_manager_set_error(manager, "Connection failed");
        TCP_LOGE("Connection failed with error: %s", strerror(sock_error));
        close(sock);
        pthread_mutex_lock(&manager->state_mutex);
        manager->state = TCP_CONN_ERROR;
        pthread_mutex_unlock(&manager->state_mutex);
        return -1;
    }
    
    // Connection successful
    pthread_mutex_lock(&manager->state_mutex);
    manager->socket_fd = sock;
    manager->state = TCP_CONN_CONNECTED;
    manager->stats.connection_start_time = time(NULL);
    manager->stats.last_activity = time(NULL);
    pthread_cond_broadcast(&manager->state_cond);
    pthread_mutex_unlock(&manager->state_mutex);
    
    // Start receiver thread
    if (pthread_create(&manager->receiver_thread, NULL, tcp_receiver_thread, manager) != 0) {
        TCP_LOGE("Failed to create receiver thread");
        tcp_connection_manager_disconnect(manager);
        return -1;
    }
    
    TCP_LOGI("Successfully connected to %s:%d", server_host, server_port);
    return 0;
}

// Disconnect from server
void tcp_connection_manager_disconnect(tcp_connection_manager_t* manager) {
    if (!manager) return;
    
    pthread_mutex_lock(&manager->state_mutex);
    
    if (manager->state == TCP_CONN_DISCONNECTED) {
        pthread_mutex_unlock(&manager->state_mutex);
        return;
    }
    
    TCP_LOGI("Disconnecting TCP connection");
    
    manager->state = TCP_CONN_DISCONNECTED;
    
    if (manager->socket_fd >= 0) {
        close(manager->socket_fd);
        manager->socket_fd = -1;
    }
    
    pthread_cond_broadcast(&manager->state_cond);
    pthread_mutex_unlock(&manager->state_mutex);
    
    // Wait for receiver thread to finish
    if (manager->receiver_thread) {
        pthread_join(manager->receiver_thread, NULL);
        manager->receiver_thread = 0;
    }
}

// Check if connected
int tcp_connection_manager_is_connected(tcp_connection_manager_t* manager) {
    if (!manager) return 0;
    
    pthread_mutex_lock(&manager->state_mutex);
    int connected = (manager->state == TCP_CONN_CONNECTED);
    pthread_mutex_unlock(&manager->state_mutex);
    
    return connected;
}

// Get current connection state
tcp_connection_state_t tcp_connection_manager_get_state(tcp_connection_manager_t* manager) {
    if (!manager) return TCP_CONN_DISCONNECTED;
    
    pthread_mutex_lock(&manager->state_mutex);
    tcp_connection_state_t state = manager->state;
    pthread_mutex_unlock(&manager->state_mutex);
    
    return state;
}

// Send protocol message
int tcp_connection_manager_send_message(tcp_connection_manager_t* manager,
                                       message_type_t type, uint32_t client_id,
                                       const void* payload, uint32_t payload_size) {
    if (!manager || !tcp_connection_manager_is_connected(manager)) {
        TCP_LOGE("Cannot send message: not connected");
        return -1;
    }
    
    char buffer[TCP_BUFFER_SIZE];
    int message_size = protocol_create_message(buffer, sizeof(buffer), type, 
                                             client_id, FLAG_NONE, payload, payload_size);
    if (message_size < 0) {
        TCP_LOGE("Failed to create protocol message");
        return -1;
    }
    
    // Send message
    ssize_t sent = send(manager->socket_fd, buffer, message_size, MSG_NOSIGNAL);
    if (sent != message_size) {
        tcp_connection_manager_set_error(manager, "Failed to send message");
        TCP_LOGE("Failed to send message: %s", strerror(errno));
        tcp_connection_manager_trigger_reconnect(manager);
        return -1;
    }
    
    tcp_connection_manager_update_stats_sent(manager, sent, 1);
    TCP_LOGD("Sent %s message (client_id=%u, size=%d)", 
         protocol_message_type_string(type), client_id, message_size);
    
    return 0;
}

// Send data message
int tcp_connection_manager_send_data(tcp_connection_manager_t* manager,
                                    uint32_t client_id, const void* data, size_t size) {
    return tcp_connection_manager_send_message(manager, MSG_DATA, client_id, data, size);
}

// Send ping message
int tcp_connection_manager_send_ping(tcp_connection_manager_t* manager) {
    return tcp_connection_manager_send_message(manager, MSG_PING, 0, NULL, 0);
}

// Register client
int tcp_connection_manager_register_client(tcp_connection_manager_t* manager,
                                          uint32_t client_id, struct sockaddr_in* client_addr) {
    if (!manager || !client_addr) return -1;
    
    client_register_payload_t payload;
    memcpy(&payload.client_addr, client_addr, sizeof(struct sockaddr_in));
    payload.capabilities = 0;
    snprintf(payload.client_info, sizeof(payload.client_info), 
             "Android client %u", client_id);
    
    return tcp_connection_manager_send_message(manager, MSG_CLIENT_REGISTER, 
                                             client_id, &payload, sizeof(payload));
}

// Configure reconnection settings
void tcp_connection_manager_configure_reconnect(tcp_connection_manager_t* manager,
                                               const tcp_reconnect_config_t* config) {
    if (!manager || !config) return;
    
    pthread_mutex_lock(&manager->state_mutex);
    manager->reconnect_config = *config;
    pthread_mutex_unlock(&manager->state_mutex);
    
    TCP_LOGI("Reconnection configured: enabled=%d, max_attempts=%d", 
         config->enabled, config->max_attempts);
}

// Enable/disable reconnection
int tcp_connection_manager_enable_reconnect(tcp_connection_manager_t* manager, int enabled) {
    if (!manager) return -1;
    
    pthread_mutex_lock(&manager->state_mutex);
    manager->reconnect_config.enabled = enabled;
    pthread_mutex_unlock(&manager->state_mutex);
    
    TCP_LOGI("Reconnection %s", enabled ? "enabled" : "disabled");
    return 0;
}

// Trigger reconnection
void tcp_connection_manager_trigger_reconnect(tcp_connection_manager_t* manager) {
    if (!manager) return;
    
    pthread_mutex_lock(&manager->state_mutex);
    
    if (manager->reconnect_config.enabled && 
        manager->state != TCP_CONN_RECONNECTING &&
        manager->state != TCP_CONN_DISCONNECTED) {
        
        TCP_LOGI("Triggering reconnection");
        manager->state = TCP_CONN_RECONNECTING;
        
        // Close current socket
        if (manager->socket_fd >= 0) {
            close(manager->socket_fd);
            manager->socket_fd = -1;
        }
        
        // Start reconnection thread if not already running
        if (!manager->reconnect_thread) {
            if (pthread_create(&manager->reconnect_thread, NULL, 
                             tcp_reconnect_thread, manager) != 0) {
                TCP_LOGE("Failed to create reconnection thread");
                manager->state = TCP_CONN_ERROR;
            }
        }
        
        pthread_cond_broadcast(&manager->state_cond);
    }
    
    pthread_mutex_unlock(&manager->state_mutex);
}

// Get connection statistics
tcp_connection_stats_t tcp_connection_manager_get_stats(tcp_connection_manager_t* manager) {
    tcp_connection_stats_t stats = {0};
    
    if (!manager) return stats;
    
    pthread_mutex_lock(&manager->stats_mutex);
    stats = manager->stats;
    pthread_mutex_unlock(&manager->stats_mutex);
    
    return stats;
}

// Reset statistics
void tcp_connection_manager_reset_stats(tcp_connection_manager_t* manager) {
    if (!manager) return;
    
    pthread_mutex_lock(&manager->stats_mutex);
    memset(&manager->stats, 0, sizeof(manager->stats));
    manager->stats.connection_start_time = time(NULL);
    pthread_mutex_unlock(&manager->stats_mutex);
    
    TCP_LOGI("Connection statistics reset");
}

// Get last error
const char* tcp_connection_manager_get_last_error(tcp_connection_manager_t* manager) {
    if (!manager || strlen(manager->last_error) == 0) {
        return "No error";
    }
    return manager->last_error;
}

// Set protocol context
void tcp_connection_manager_set_protocol_context(tcp_connection_manager_t* manager,
                                                 tcp_protocol_ctx_t* protocol_ctx) {
    if (!manager) return;
    
    pthread_mutex_lock(&manager->state_mutex);
    manager->protocol_ctx = protocol_ctx;
    pthread_mutex_unlock(&manager->state_mutex);
    
    TCP_LOGI("Protocol context set");
}

// Helper function to update sent statistics
void tcp_connection_manager_update_stats_sent(tcp_connection_manager_t* manager,
                                              size_t bytes, int is_message) {
    if (!manager) return;
    
    pthread_mutex_lock(&manager->stats_mutex);
    manager->stats.bytes_sent += bytes;
    if (is_message) {
        manager->stats.messages_sent++;
    }
    manager->stats.last_activity = time(NULL);
    pthread_mutex_unlock(&manager->stats_mutex);
}

// Helper function to update received statistics  
void tcp_connection_manager_update_stats_received(tcp_connection_manager_t* manager,
                                                  size_t bytes, int is_message) {
    if (!manager) return;
    
    pthread_mutex_lock(&manager->stats_mutex);
    manager->stats.bytes_received += bytes;
    if (is_message) {
        manager->stats.messages_received++;
    }
    manager->stats.last_activity = time(NULL);
    pthread_mutex_unlock(&manager->stats_mutex);
}

// Set error message
void tcp_connection_manager_set_error(tcp_connection_manager_t* manager, const char* error) {
    if (!manager || !error) return;
    
    strncpy(manager->last_error, error, sizeof(manager->last_error) - 1);
    manager->last_error[sizeof(manager->last_error) - 1] = '\0';
    manager->last_error_time = time(NULL);
    
    TCP_LOGE("TCP connection error: %s", error);
}

// Convert state to string
const char* tcp_connection_state_string(tcp_connection_state_t state) {
    switch (state) {
        case TCP_CONN_DISCONNECTED: return "DISCONNECTED";
        case TCP_CONN_CONNECTING: return "CONNECTING";
        case TCP_CONN_CONNECTED: return "CONNECTED";
        case TCP_CONN_RECONNECTING: return "RECONNECTING";
        case TCP_CONN_ERROR: return "ERROR";
        default: return "UNKNOWN";
    }
}

// Calculate reconnection delay with exponential backoff and jitter
int tcp_connection_manager_calculate_reconnect_delay(tcp_connection_manager_t* manager) {
    if (!manager) return 1000;
    
    int delay = manager->current_delay_ms;
    
    // Add random jitter
    if (manager->reconnect_config.jitter_ms > 0) {
        int jitter = rand() % manager->reconnect_config.jitter_ms;
        delay += jitter;
    }
    
    // Update delay for next attempt (exponential backoff)
    manager->current_delay_ms = (int)(manager->current_delay_ms * 
                                     manager->reconnect_config.backoff_multiplier);
    
    // Cap at maximum delay
    if (manager->current_delay_ms > manager->reconnect_config.max_delay_ms) {
        manager->current_delay_ms = manager->reconnect_config.max_delay_ms;
    }
    
    return delay;
}

// TCP receiver thread
void* tcp_receiver_thread(void* arg) {
    tcp_connection_manager_t* manager = (tcp_connection_manager_t*)arg;
    char buffer[TCP_BUFFER_SIZE];
    
    TCP_LOGI("TCP receiver thread started");
    
    while (!manager->shutdown_flag) {
        pthread_mutex_lock(&manager->state_mutex);
        if (manager->state != TCP_CONN_CONNECTED || manager->socket_fd < 0) {
            pthread_mutex_unlock(&manager->state_mutex);
            break;
        }
        int sock = manager->socket_fd;
        pthread_mutex_unlock(&manager->state_mutex);
        
        // Use select with timeout for cancellable I/O
        fd_set read_fds;
        struct timeval timeout = {1, 0}; // 1 second timeout
        
        FD_ZERO(&read_fds);
        FD_SET(sock, &read_fds);
        
        int select_result = select(sock + 1, &read_fds, NULL, NULL, &timeout);
        if (select_result < 0) {
            TCP_LOGE("Select error in receiver thread: %s", strerror(errno));
            tcp_connection_manager_trigger_reconnect(manager);
            break;
        } else if (select_result == 0) {
            // Timeout, continue loop
            continue;
        }
        
        if (FD_ISSET(sock, &read_fds)) {
            ssize_t received = recv(sock, buffer, sizeof(buffer), 0);
            if (received <= 0) {
                if (received == 0) {
                    TCP_LOGI("Connection closed by server");
                } else {
                    TCP_LOGE("Receive error: %s", strerror(errno));
                }
                tcp_connection_manager_trigger_reconnect(manager);
                break;
            }
            
            tcp_connection_manager_update_stats_received(manager, received, 0);
            tcp_connection_manager_handle_received_data(manager, buffer, received);
        }
    }
    
    TCP_LOGI("TCP receiver thread finished");
    return NULL;
}

// TCP reconnection thread
void* tcp_reconnect_thread(void* arg) {
    tcp_connection_manager_t* manager = (tcp_connection_manager_t*)arg;
    
    TCP_LOGI("TCP reconnection thread started");
    
    while (!manager->shutdown_flag) {
        pthread_mutex_lock(&manager->state_mutex);
        
        if (manager->state != TCP_CONN_RECONNECTING) {
            pthread_mutex_unlock(&manager->state_mutex);
            break;
        }
        
        // Check if we've exceeded maximum attempts
        if (manager->reconnect_config.max_attempts > 0 && 
            manager->current_reconnect_attempt >= manager->reconnect_config.max_attempts) {
            TCP_LOGE("Maximum reconnection attempts exceeded");
            manager->state = TCP_CONN_ERROR;
            pthread_cond_broadcast(&manager->state_cond);
            pthread_mutex_unlock(&manager->state_mutex);
            break;
        }
        
        manager->current_reconnect_attempt++;
        int delay = tcp_connection_manager_calculate_reconnect_delay(manager);
        
        pthread_mutex_unlock(&manager->state_mutex);
        
        TCP_LOGI("Reconnection attempt %d after %d ms delay", 
             manager->current_reconnect_attempt, delay);
        
        // Sleep with cancellation
        usleep(delay * 1000);
        
        if (manager->shutdown_flag) break;
        
        // Attempt reconnection
        if (tcp_connection_manager_connect(manager, manager->server_host, 
                                          manager->server_port) == 0) {
            pthread_mutex_lock(&manager->stats_mutex);
            manager->stats.reconnect_count++;
            pthread_mutex_unlock(&manager->stats_mutex);
            
            TCP_LOGI("Reconnection successful after %d attempts", 
                 manager->current_reconnect_attempt);
            break;
        }
    }
    
    // Reset reconnection thread handle
    pthread_mutex_lock(&manager->state_mutex);
    manager->reconnect_thread = 0;
    pthread_mutex_unlock(&manager->state_mutex);
    
    TCP_LOGI("TCP reconnection thread finished");
    return NULL;
}

// Handle received data
int tcp_connection_manager_handle_received_data(tcp_connection_manager_t* manager,
                                               const char* data, size_t size) {
    if (!manager || !data || size == 0) return -1;
    
    TCP_LOGD("Received %zu bytes of data", size);
    
    // Parse protocol header
    udp_bridge_header_t header;
    int parse_result = protocol_parse_header(data, size, &header);
    if (parse_result < 0) {
        TCP_LOGW("Failed to parse protocol header");
        return -1;
    }
    
    // Validate header
    if (protocol_validate_header(&header) < 0) {
        TCP_LOGW("Invalid protocol header");
        return -1;
    }
    
    tcp_connection_manager_update_stats_received(manager, size, 1);
    
    TCP_LOGD("Received %s message (client_id=%u, payload_size=%u)",
         protocol_message_type_string(header.message_type), 
         header.client_id, header.payload_size);
    
    // Handle different message types
    switch (header.message_type) {
        case MSG_DATA:
            // Forward data to protocol context if available
            if (manager->protocol_ctx) {
                const char* payload = data + UDP_BRIDGE_HEADER_SIZE;
                // android_handle_protocol_response(manager->protocol_ctx, payload, header.payload_size);
                TCP_LOGD("Would forward %u bytes to protocol context", header.payload_size);
            }
            break;
            
        case MSG_PONG:
            TCP_LOGD("Received pong response");
            break;
            
        case MSG_ERROR: {
            error_payload_t error_payload;
            if (header.payload_size >= sizeof(error_payload)) {
                memcpy(&error_payload, data + UDP_BRIDGE_HEADER_SIZE, sizeof(error_payload));
                TCP_LOGE("Received error from server: %s (code=%u)", 
                     error_payload.error_message, error_payload.error_code);
                tcp_connection_manager_set_error(manager, error_payload.error_message);
            }
            break;
        }
        
        default:
            TCP_LOGW("Received unknown message type: %d", header.message_type);
            break;
    }
    
    return 0;
}
