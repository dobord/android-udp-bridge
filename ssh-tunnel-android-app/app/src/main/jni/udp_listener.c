#include "udp_listener.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/select.h>
#include <arpa/inet.h>

// Create UDP listener context
udp_listener_ctx_t* udp_listener_create(int local_port) {
    UDP_LOGI("Creating UDP listener on port %d", local_port);
    
    udp_listener_ctx_t* ctx = (udp_listener_ctx_t*)calloc(1, sizeof(udp_listener_ctx_t));
    if (!ctx) {
        UDP_LOGE("Failed to allocate UDP listener context");
        return NULL;
    }
    
    // Initialize context
    ctx->udp_socket = -1;
    ctx->local_port = local_port;
    ctx->should_stop = 0;
    ctx->bridge_connected = 0;
    ctx->bridge_server_port = 0;
    memset(ctx->bridge_server_host, 0, sizeof(ctx->bridge_server_host));
    
    // Initialize mutex
    if (pthread_mutex_init(&ctx->stop_mutex, NULL) != 0) {
        UDP_LOGE("Failed to initialize stop mutex");
        free(ctx);
        return NULL;
    }
    
    // Initialize protocol context
    ctx->protocol_ctx = (android_protocol_ctx_t*)calloc(1, sizeof(android_protocol_ctx_t));
    if (!ctx->protocol_ctx) {
        UDP_LOGE("Failed to allocate protocol context");
        pthread_mutex_destroy(&ctx->stop_mutex);
        free(ctx);
        return NULL;
    }
    
    // Initialize protocol
    if (android_protocol_init(ctx->protocol_ctx, local_port) != 0) {
        UDP_LOGE("Failed to initialize protocol context");
        free(ctx->protocol_ctx);
        pthread_mutex_destroy(&ctx->stop_mutex);
        free(ctx);
        return NULL;
    }
    
    UDP_LOGI("UDP listener created successfully");
    return ctx;
}

// Destroy UDP listener context
void udp_listener_destroy(udp_listener_ctx_t* ctx) {
    if (!ctx) return;
    
    UDP_LOGI("Destroying UDP listener");
    
    // Stop listener if running
    udp_listener_stop(ctx);
    
    // Disconnect from bridge
    udp_listener_disconnect_bridge(ctx);
    
    // Cleanup protocol context
    if (ctx->protocol_ctx) {
        android_protocol_cleanup(ctx->protocol_ctx);
        free(ctx->protocol_ctx);
    }
    
    // Close UDP socket
    if (ctx->udp_socket >= 0) {
        close(ctx->udp_socket);
    }
    
    // Cleanup mutex
    pthread_mutex_destroy(&ctx->stop_mutex);
    
    free(ctx);
    UDP_LOGI("UDP listener destroyed");
}

// Set bridge server configuration
int udp_listener_set_bridge_server(udp_listener_ctx_t* ctx, const char* host, int port) {
    if (!ctx || !host || port <= 0 || port > 65535) {
        UDP_LOGE("Invalid parameters for bridge server configuration");
        return -1;
    }
    
    strncpy(ctx->bridge_server_host, host, sizeof(ctx->bridge_server_host) - 1);
    ctx->bridge_server_host[sizeof(ctx->bridge_server_host) - 1] = '\0';
    ctx->bridge_server_port = port;
    
    UDP_LOGI("Bridge server configured: %s:%d", host, port);
    return 0;
}

// Connect to bridge server
int udp_listener_connect_bridge(udp_listener_ctx_t* ctx) {
    if (!ctx || !ctx->protocol_ctx) {
        UDP_LOGE("Invalid context for bridge connection");
        return -1;
    }
    
    if (ctx->bridge_server_host[0] == '\0' || ctx->bridge_server_port == 0) {
        UDP_LOGE("Bridge server not configured");
        return -1;
    }
    
    UDP_LOGI("Connecting to bridge server %s:%d", 
             ctx->bridge_server_host, ctx->bridge_server_port);
    
    int result = android_bridge_connect(ctx->protocol_ctx, 
                                       ctx->bridge_server_host, 
                                       ctx->bridge_server_port);
    
    if (result == 0) {
        ctx->bridge_connected = 1;
        UDP_LOGI("Bridge connection established");
    } else {
        UDP_LOGE("Failed to connect to bridge server");
    }
    
    return result;
}

// Disconnect from bridge server
void udp_listener_disconnect_bridge(udp_listener_ctx_t* ctx) {
    if (!ctx || !ctx->protocol_ctx) return;
    
    if (ctx->bridge_connected) {
        UDP_LOGI("Disconnecting from bridge server");
        
        // Close TCP connection
        if (ctx->protocol_ctx->tcp_socket >= 0) {
            close(ctx->protocol_ctx->tcp_socket);
            ctx->protocol_ctx->tcp_socket = -1;
        }
        
        ctx->bridge_connected = 0;
        UDP_LOGI("Bridge disconnected");
    }
}

// Start UDP listener
int udp_listener_start(udp_listener_ctx_t* ctx) {
    if (!ctx) {
        UDP_LOGE("Invalid context for UDP listener start");
        return -1;
    }
    
    pthread_mutex_lock(&ctx->stop_mutex);
    
    if (ctx->listener_thread != 0) {
        UDP_LOGW("UDP listener already running");
        pthread_mutex_unlock(&ctx->stop_mutex);
        return 0;
    }
    
    // Create UDP socket
    ctx->udp_socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (ctx->udp_socket < 0) {
        UDP_LOGE("Failed to create UDP socket: %s", strerror(errno));
        pthread_mutex_unlock(&ctx->stop_mutex);
        return -1;
    }
    
    // Set socket options
    int reuse = 1;
    if (setsockopt(ctx->udp_socket, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
        UDP_LOGW("Failed to set SO_REUSEADDR: %s", strerror(errno));
    }
    
    // Set socket timeout
    struct timeval timeout;
    timeout.tv_sec = UDP_LISTENER_TIMEOUT / 1000;
    timeout.tv_usec = (UDP_LISTENER_TIMEOUT % 1000) * 1000;
    if (setsockopt(ctx->udp_socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0) {
        UDP_LOGW("Failed to set socket timeout: %s", strerror(errno));
    }
    
    // Bind socket
    struct sockaddr_in bind_addr;
    memset(&bind_addr, 0, sizeof(bind_addr));
    bind_addr.sin_family = AF_INET;
    bind_addr.sin_addr.s_addr = INADDR_ANY;
    bind_addr.sin_port = htons(ctx->local_port);
    
    if (bind(ctx->udp_socket, (struct sockaddr*)&bind_addr, sizeof(bind_addr)) < 0) {
        UDP_LOGE("Failed to bind UDP socket to port %d: %s", ctx->local_port, strerror(errno));
        close(ctx->udp_socket);
        ctx->udp_socket = -1;
        pthread_mutex_unlock(&ctx->stop_mutex);
        return -1;
    }
    
    // Start listener thread
    ctx->should_stop = 0;
    int result = pthread_create(&ctx->listener_thread, NULL, udp_listener_thread_func, ctx);
    if (result != 0) {
        UDP_LOGE("Failed to create UDP listener thread: %s", strerror(result));
        close(ctx->udp_socket);
        ctx->udp_socket = -1;
        pthread_mutex_unlock(&ctx->stop_mutex);
        return -1;
    }
    
    pthread_mutex_unlock(&ctx->stop_mutex);
    
    UDP_LOGI("UDP listener started on port %d", ctx->local_port);
    return 0;
}

// Stop UDP listener
void udp_listener_stop(udp_listener_ctx_t* ctx) {
    if (!ctx) return;
    
    pthread_mutex_lock(&ctx->stop_mutex);
    
    if (ctx->listener_thread == 0) {
        UDP_LOGW("UDP listener not running");
        pthread_mutex_unlock(&ctx->stop_mutex);
        return;
    }
    
    UDP_LOGI("Stopping UDP listener");
    
    // Signal thread to stop
    ctx->should_stop = 1;
    
    pthread_mutex_unlock(&ctx->stop_mutex);
    
    // Wait for thread to finish
    pthread_join(ctx->listener_thread, NULL);
    ctx->listener_thread = 0;
    
    // Close socket
    if (ctx->udp_socket >= 0) {
        close(ctx->udp_socket);
        ctx->udp_socket = -1;
    }
    
    UDP_LOGI("UDP listener stopped");
}

// Check if UDP listener is running
int udp_listener_is_running(udp_listener_ctx_t* ctx) {
    if (!ctx) return 0;
    
    pthread_mutex_lock(&ctx->stop_mutex);
    int running = (ctx->listener_thread != 0 && !ctx->should_stop);
    pthread_mutex_unlock(&ctx->stop_mutex);
    
    return running;
}

// Handle incoming UDP packet
int udp_listener_handle_packet(udp_listener_ctx_t* ctx, const char* data, 
                              size_t size, struct sockaddr_in* from_addr) {
    if (!ctx || !data || size == 0 || !from_addr) {
        UDP_LOGE("Invalid parameters for packet handling");
        return -1;
    }
    
    // Identify or create client
    uint32_t client_id = udp_listener_identify_client(ctx, from_addr);
    if (client_id == 0) {
        UDP_LOGE("Failed to identify client");
        ctx->error_count++;
        return -1;
    }
    
    // Update client activity
    udp_listener_update_client_activity(ctx, client_id);
    
    // Send packet through protocol bridge
    if (!ctx->bridge_connected) {
        UDP_LOGW("Bridge not connected, dropping packet from client %u", client_id);
        return -1;
    }
    
    int result = android_send_protocol_message(ctx->protocol_ctx, MSG_DATA, 
                                              client_id, data, size);
    if (result == 0) {
        // Update statistics
        ctx->packets_received++;
        ctx->bytes_received += size;
        
        UDP_LOGD("Forwarded %zu bytes from client %u to bridge", size, client_id);
    } else {
        UDP_LOGE("Failed to forward packet from client %u", client_id);
        ctx->error_count++;
    }
    
    return result;
}

// Send data to specific client
int udp_listener_send_to_client(udp_listener_ctx_t* ctx, uint32_t client_id, 
                               const char* data, size_t size) {
    if (!ctx || !data || size == 0) {
        UDP_LOGE("Invalid parameters for sending to client");
        return -1;
    }
    
    // Find client by ID
    client_entry_t* client = android_find_client_by_id(ctx->protocol_ctx, client_id);
    if (!client) {
        UDP_LOGE("Client %u not found", client_id);
        return -1;
    }
    
    // Send UDP packet to client
    ssize_t sent = sendto(ctx->udp_socket, data, size, 0, 
                         (struct sockaddr*)&client->client_addr, 
                         sizeof(client->client_addr));
    
    if (sent < 0) {
        UDP_LOGE("Failed to send data to client %u: %s", client_id, strerror(errno));
        ctx->error_count++;
        return -1;
    }
    
    if ((size_t)sent != size) {
        UDP_LOGW("Partial send to client %u: %zd/%zu bytes", client_id, sent, size);
    }
    
    // Update statistics
    ctx->packets_sent++;
    ctx->bytes_sent += sent;
    
    // Update client statistics
    android_update_client_stats(ctx->protocol_ctx, client_id, 0, sent);
    
    UDP_LOGD("Sent %zd bytes to client %u", sent, client_id);
    return 0;
}

// Get statistics
void udp_listener_get_stats(udp_listener_ctx_t* ctx, uint64_t* packets_rx, 
                           uint64_t* packets_tx, uint64_t* bytes_rx, 
                           uint64_t* bytes_tx, uint32_t* errors) {
    if (!ctx) return;
    
    if (packets_rx) *packets_rx = ctx->packets_received;
    if (packets_tx) *packets_tx = ctx->packets_sent;
    if (bytes_rx) *bytes_rx = ctx->bytes_received;
    if (bytes_tx) *bytes_tx = ctx->bytes_sent;
    if (errors) *errors = ctx->error_count;
}

// Reset statistics
void udp_listener_reset_stats(udp_listener_ctx_t* ctx) {
    if (!ctx) return;
    
    ctx->packets_received = 0;
    ctx->packets_sent = 0;
    ctx->bytes_received = 0;
    ctx->bytes_sent = 0;
    ctx->error_count = 0;
    
    UDP_LOGI("UDP listener statistics reset");
}

// Identify client (get or assign client ID)
uint32_t udp_listener_identify_client(udp_listener_ctx_t* ctx, struct sockaddr_in* addr) {
    if (!ctx || !ctx->protocol_ctx || !addr) {
        return 0;
    }
    
    return android_add_or_update_client(ctx->protocol_ctx, addr);
}

// Update client activity
int udp_listener_update_client_activity(udp_listener_ctx_t* ctx, uint32_t client_id) {
    if (!ctx || !ctx->protocol_ctx || client_id == 0) {
        return -1;
    }
    
    // Find client and update last activity
    client_entry_t* client = android_find_client_by_id(ctx->protocol_ctx, client_id);
    if (client) {
        client->last_activity = time(NULL);
        client->packets_received++;
        return 0;
    }
    
    return -1;
}

// Cleanup expired clients
int udp_listener_cleanup_expired_clients(udp_listener_ctx_t* ctx) {
    if (!ctx || !ctx->protocol_ctx) {
        return -1;
    }
    
    return android_cleanup_expired_clients(ctx->protocol_ctx);
}

// Main UDP listener thread function
void* udp_listener_thread_func(void* arg) {
    udp_listener_ctx_t* ctx = (udp_listener_ctx_t*)arg;
    if (!ctx) {
        UDP_LOGE("Invalid context in listener thread");
        return NULL;
    }
    
    UDP_LOGI("UDP listener thread started");
    
    char buffer[UDP_BUFFER_SIZE];
    struct sockaddr_in from_addr;
    socklen_t from_len;
    ssize_t received;
    time_t last_cleanup = time(NULL);
    
    while (!ctx->should_stop) {
        from_len = sizeof(from_addr);
        
        // Receive UDP packet
        received = recvfrom(ctx->udp_socket, buffer, sizeof(buffer), 0,
                           (struct sockaddr*)&from_addr, &from_len);
        
        if (received < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // Timeout, check for stop condition and continue
                continue;
            } else {
                UDP_LOGE("Error receiving UDP packet: %s", strerror(errno));
                ctx->error_count++;
                continue;
            }
        }
        
        if (received == 0) {
            UDP_LOGW("Received empty UDP packet");
            continue;
        }
        
        // Handle the packet
        udp_listener_handle_packet(ctx, buffer, received, &from_addr);
        
        // Periodic cleanup of expired clients
        time_t now = time(NULL);
        if (now - last_cleanup >= CLIENT_CLEANUP_INTERVAL) {
            udp_listener_cleanup_expired_clients(ctx);
            last_cleanup = now;
        }
    }
    
    UDP_LOGI("UDP listener thread stopped");
    return NULL;
}
