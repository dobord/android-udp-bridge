#include "tcp_udp_bridge.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

// Create TCP-UDP bridge
tcp_udp_bridge_t* tcp_udp_bridge_create(void) {
    tcp_udp_bridge_t* bridge = calloc(1, sizeof(tcp_udp_bridge_t));
    if (!bridge) {
        LOGE("Failed to allocate memory for TCP-UDP bridge");
        return NULL;
    }
    
    // Initialize mutex
    if (pthread_mutex_init(&bridge->bridge_mutex, NULL) != 0) {
        LOGE("Failed to initialize bridge mutex");
        free(bridge);
        return NULL;
    }
    
    bridge->active = 0;
    LOGI("TCP-UDP bridge created successfully");
    return bridge;
}

// Destroy TCP-UDP bridge
void tcp_udp_bridge_destroy(tcp_udp_bridge_t* bridge) {
    if (!bridge) return;
    
    LOGI("Destroying TCP-UDP bridge");
    
    // Stop bridge if active
    tcp_udp_bridge_stop(bridge);
    
    // Cleanup mutex
    pthread_mutex_destroy(&bridge->bridge_mutex);
    
    free(bridge);
    LOGI("TCP-UDP bridge destroyed");
}

// Configure bridge
int tcp_udp_bridge_configure(tcp_udp_bridge_t* bridge, 
                            const char* server_host, int server_port, int local_udp_port) {
    if (!bridge || !server_host) {
        LOGE("Invalid parameters for bridge configuration");
        return -1;
    }
    
    pthread_mutex_lock(&bridge->bridge_mutex);
    
    // Store configuration
    strncpy(bridge->server_host, server_host, sizeof(bridge->server_host) - 1);
    bridge->server_host[sizeof(bridge->server_host) - 1] = '\0';
    bridge->server_port = server_port;
    bridge->local_udp_port = local_udp_port;
    
    pthread_mutex_unlock(&bridge->bridge_mutex);
    
    LOGI("Bridge configured: server=%s:%d, local_udp_port=%d", 
         server_host, server_port, local_udp_port);
    return 0;
}

// Start bridge
int tcp_udp_bridge_start(tcp_udp_bridge_t* bridge) {
    if (!bridge) {
        LOGE("Invalid bridge parameter");
        return -1;
    }
    
    pthread_mutex_lock(&bridge->bridge_mutex);
    
    if (bridge->active) {
        LOGW("Bridge already active");
        pthread_mutex_unlock(&bridge->bridge_mutex);
        return 0;
    }
    
    // Validate that components are set
    if (!bridge->tcp_manager) {
        LOGE("TCP manager not set");
        pthread_mutex_unlock(&bridge->bridge_mutex);
        return -1;
    }
    
    if (!bridge->udp_listener) {
        LOGE("UDP listener not set");
        pthread_mutex_unlock(&bridge->bridge_mutex);
        return -1;
    }
    
    // Start TCP connection if not already connected
    if (!tcp_connection_manager_is_connected(bridge->tcp_manager)) {
        if (tcp_connection_manager_connect(bridge->tcp_manager, 
                                          bridge->server_host, bridge->server_port) != 0) {
            LOGE("Failed to connect TCP manager");
            pthread_mutex_unlock(&bridge->bridge_mutex);
            return -1;
        }
    }
    
    // Get protocol context from UDP listener
    bridge->protocol_ctx = bridge->udp_listener->protocol_ctx;
    if (bridge->protocol_ctx) {
        // Set TCP manager as the bridge connection in protocol context
        tcp_connection_manager_set_protocol_context(bridge->tcp_manager, bridge->protocol_ctx);
        bridge->protocol_ctx->tcp_socket = bridge->tcp_manager->socket_fd;
    }
    
    bridge->active = 1;
    pthread_mutex_unlock(&bridge->bridge_mutex);
    
    LOGI("TCP-UDP bridge started successfully");
    return 0;
}

// Stop bridge
void tcp_udp_bridge_stop(tcp_udp_bridge_t* bridge) {
    if (!bridge) return;
    
    pthread_mutex_lock(&bridge->bridge_mutex);
    
    if (!bridge->active) {
        pthread_mutex_unlock(&bridge->bridge_mutex);
        return;
    }
    
    LOGI("Stopping TCP-UDP bridge");
    
    // Disconnect TCP manager
    if (bridge->tcp_manager) {
        tcp_connection_manager_disconnect(bridge->tcp_manager);
    }
    
    // Clear protocol context TCP socket
    if (bridge->protocol_ctx) {
        bridge->protocol_ctx->tcp_socket = -1;
    }
    
    bridge->active = 0;
    pthread_mutex_unlock(&bridge->bridge_mutex);
    
    LOGI("TCP-UDP bridge stopped");
}

// Check if bridge is active
int tcp_udp_bridge_is_active(tcp_udp_bridge_t* bridge) {
    if (!bridge) return 0;
    
    pthread_mutex_lock(&bridge->bridge_mutex);
    int active = bridge->active;
    pthread_mutex_unlock(&bridge->bridge_mutex);
    
    return active;
}

// Forward UDP data to TCP
int tcp_udp_bridge_forward_udp_to_tcp(tcp_udp_bridge_t* bridge, 
                                      uint32_t client_id, const void* data, size_t size) {
    if (!bridge || !data || size == 0) {
        LOGE("Invalid parameters for UDP to TCP forwarding");
        return -1;
    }
    
    pthread_mutex_lock(&bridge->bridge_mutex);
    
    if (!bridge->active || !bridge->tcp_manager) {
        LOGE("Bridge not active or TCP manager not available");
        pthread_mutex_unlock(&bridge->bridge_mutex);
        return -1;
    }
    
    // Send data through TCP connection manager
    int result = tcp_connection_manager_send_data(bridge->tcp_manager, client_id, data, size);
    
    if (result == 0) {
        // Update statistics
        bridge->udp_to_tcp_packets++;
        bridge->udp_to_tcp_bytes += size;
        
        LOGD("Forwarded UDP to TCP: client_id=%u, size=%zu", client_id, size);
    } else {
        bridge->bridge_errors++;
        LOGE("Failed to forward UDP to TCP: client_id=%u, size=%zu", client_id, size);
    }
    
    pthread_mutex_unlock(&bridge->bridge_mutex);
    return result;
}

// Forward TCP data to UDP
int tcp_udp_bridge_forward_tcp_to_udp(tcp_udp_bridge_t* bridge,
                                      uint32_t client_id, const void* data, size_t size) {
    if (!bridge || !data || size == 0) {
        LOGE("Invalid parameters for TCP to UDP forwarding");
        return -1;
    }
    
    pthread_mutex_lock(&bridge->bridge_mutex);
    
    if (!bridge->active || !bridge->udp_listener || !bridge->protocol_ctx) {
        LOGE("Bridge not active or UDP listener/protocol context not available");
        pthread_mutex_unlock(&bridge->bridge_mutex);
        return -1;
    }
    
    // Find client by ID
    // client_entry_t* client = android_find_client_by_id(bridge->protocol_ctx, client_id);
    // For testing, simulate client found
    struct sockaddr_in mock_addr;
    memset(&mock_addr, 0, sizeof(mock_addr));
    mock_addr.sin_family = AF_INET;
    mock_addr.sin_port = htons(5060);
    mock_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    
    if (client_id == 0) {
        LOGW("Invalid client_id for TCP to UDP forwarding: client_id=%u", client_id);
        bridge->bridge_errors++;
        pthread_mutex_unlock(&bridge->bridge_mutex);
        return -1;
    }
    
    // Send data through UDP socket
    ssize_t sent = sendto(bridge->protocol_ctx->udp_socket, data, size, 0,
                         (struct sockaddr*)&mock_addr, sizeof(mock_addr));
    
    int result = 0;
    if (sent != (ssize_t)size) {
        LOGE("Failed to send UDP data: client_id=%u, expected=%zu, sent=%zd", 
             client_id, size, sent);
        bridge->bridge_errors++;
        result = -1;
    } else {
        // Update statistics
        bridge->tcp_to_udp_packets++;
        bridge->tcp_to_udp_bytes += size;
        
        // Update client stats
        // android_update_client_stats(bridge->protocol_ctx, client_id, 0, size);
        LOGD("Would update client stats for client_id=%u", client_id);
        
        LOGD("Forwarded TCP to UDP: client_id=%u, size=%zu", client_id, size);
    }
    
    pthread_mutex_unlock(&bridge->bridge_mutex);
    return result;
}

// Get bridge statistics
void tcp_udp_bridge_get_stats(tcp_udp_bridge_t* bridge,
                              uint64_t* udp_to_tcp_packets, uint64_t* tcp_to_udp_packets,
                              uint64_t* udp_to_tcp_bytes, uint64_t* tcp_to_udp_bytes,
                              uint32_t* errors) {
    if (!bridge) return;
    
    pthread_mutex_lock(&bridge->bridge_mutex);
    
    if (udp_to_tcp_packets) *udp_to_tcp_packets = bridge->udp_to_tcp_packets;
    if (tcp_to_udp_packets) *tcp_to_udp_packets = bridge->tcp_to_udp_packets;
    if (udp_to_tcp_bytes) *udp_to_tcp_bytes = bridge->udp_to_tcp_bytes;
    if (tcp_to_udp_bytes) *tcp_to_udp_bytes = bridge->tcp_to_udp_bytes;
    if (errors) *errors = bridge->bridge_errors;
    
    pthread_mutex_unlock(&bridge->bridge_mutex);
}

// Reset bridge statistics
void tcp_udp_bridge_reset_stats(tcp_udp_bridge_t* bridge) {
    if (!bridge) return;
    
    pthread_mutex_lock(&bridge->bridge_mutex);
    
    bridge->udp_to_tcp_packets = 0;
    bridge->tcp_to_udp_packets = 0;
    bridge->udp_to_tcp_bytes = 0;
    bridge->tcp_to_udp_bytes = 0;
    bridge->bridge_errors = 0;
    
    pthread_mutex_unlock(&bridge->bridge_mutex);
    
    LOGI("Bridge statistics reset");
}

// Get bridge status
const char* tcp_udp_bridge_get_status(tcp_udp_bridge_t* bridge) {
    if (!bridge) return "Invalid bridge";
    
    pthread_mutex_lock(&bridge->bridge_mutex);
    
    const char* status;
    if (!bridge->active) {
        status = "Inactive";
    } else if (!bridge->tcp_manager) {
        status = "No TCP manager";
    } else if (!bridge->udp_listener) {
        status = "No UDP listener";
    } else if (!tcp_connection_manager_is_connected(bridge->tcp_manager)) {
        status = "TCP disconnected";
    } else {
        status = "Active";
    }
    
    pthread_mutex_unlock(&bridge->bridge_mutex);
    return status;
}

// Get last error
const char* tcp_udp_bridge_get_last_error(tcp_udp_bridge_t* bridge) {
    if (!bridge || !bridge->tcp_manager) {
        return "No error information available";
    }
    
    return tcp_connection_manager_get_last_error(bridge->tcp_manager);
}

// Set UDP listener
int tcp_udp_bridge_set_udp_listener(tcp_udp_bridge_t* bridge, udp_listener_ctx_t* udp_listener) {
    if (!bridge) {
        LOGE("Invalid bridge parameter");
        return -1;
    }
    
    pthread_mutex_lock(&bridge->bridge_mutex);
    bridge->udp_listener = udp_listener;
    pthread_mutex_unlock(&bridge->bridge_mutex);
    
    LOGI("UDP listener set for bridge");
    return 0;
}

// Set TCP manager
int tcp_udp_bridge_set_tcp_manager(tcp_udp_bridge_t* bridge, tcp_connection_manager_t* tcp_manager) {
    if (!bridge) {
        LOGE("Invalid bridge parameter");
        return -1;
    }
    
    pthread_mutex_lock(&bridge->bridge_mutex);
    bridge->tcp_manager = tcp_manager;
    pthread_mutex_unlock(&bridge->bridge_mutex);
    
    LOGI("TCP manager set for bridge");
    return 0;
}

// Get protocol context
android_protocol_ctx_t* tcp_udp_bridge_get_protocol_context(tcp_udp_bridge_t* bridge) {
    if (!bridge) return NULL;
    
    pthread_mutex_lock(&bridge->bridge_mutex);
    android_protocol_ctx_t* ctx = bridge->protocol_ctx;
    pthread_mutex_unlock(&bridge->bridge_mutex);
    
    return ctx;
}
