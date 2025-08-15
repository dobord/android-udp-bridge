#include "udp_bridge_protocol.h"
#include <string.h>
// Legacy stub: implementation migrated to legacy/udp_bridge_protocol.c
#include "legacy/udp_bridge_protocol.c"

#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <pthread.h>

// CRC32 table for checksum calculation
static const uint32_t crc32_table[256] = {
    // Legacy stub: implementation migrated to legacy/udp_bridge_protocol.c
    #include "legacy/udp_bridge_protocol.c"
        LOGE("Failed to bind UDP socket to port %d: %s", local_port, strerror(errno));
        close(ctx->udp_socket);
        pthread_mutex_destroy(&ctx->mutex);
        client_manager_destroy(ctx->client_manager);
        return -1;
    }
    
    LOGI("Protocol initialized on UDP port %d with client manager", local_port);
    return 0;
}

void android_protocol_cleanup(android_protocol_ctx_t* ctx) {
    if (!ctx) {
        return;
    }
    
    pthread_mutex_lock(&ctx->mutex);
    
    // Close sockets
    if (ctx->tcp_socket >= 0) {
        close(ctx->tcp_socket);
        ctx->tcp_socket = -1;
    }
    
    if (ctx->udp_socket >= 0) {
        close(ctx->udp_socket);
        ctx->udp_socket = -1;
    }
    
    pthread_mutex_unlock(&ctx->mutex);
    pthread_mutex_destroy(&ctx->mutex);
    
    // Destroy client manager
    if (ctx->client_manager) {
        client_manager_destroy(ctx->client_manager);
        ctx->client_manager = NULL;
    }
    
    LOGI("Protocol cleanup completed");
}

uint32_t android_add_or_update_client(android_protocol_ctx_t* ctx, struct sockaddr_in* addr) {
    if (!ctx || !addr || !ctx->client_manager) {
        LOGE("Invalid parameters for android_add_or_update_client");
        return 0;
    }
    
    uint32_t client_id = client_manager_add_client(ctx->client_manager, addr);
    if (client_id > 0) {
        LOGD("Client added/updated: ID=%u, addr=%s:%d", 
             client_id, inet_ntoa(addr->sin_addr), ntohs(addr->sin_port));
    }
    
    return client_id;
}

client_entry_t* android_find_client_by_id(android_protocol_ctx_t* ctx, uint32_t client_id) {
    if (!ctx || !ctx->client_manager || client_id == 0) {
        return NULL;
    }
    
    return client_manager_find_by_id(ctx->client_manager, client_id);
}

client_entry_t* android_find_client_by_addr(android_protocol_ctx_t* ctx, struct sockaddr_in* addr) {
    if (!ctx || !ctx->client_manager || !addr) {
        return NULL;
    }
    
    return client_manager_find_by_addr(ctx->client_manager, addr);
}

int android_update_client_stats(android_protocol_ctx_t* ctx, uint32_t client_id, 
                               uint32_t bytes_received, uint32_t bytes_sent) {
    if (!ctx || !ctx->client_manager) {
        return -1;
    }
    
    return client_manager_update_stats(ctx->client_manager, client_id, bytes_received, bytes_sent);
}

int android_cleanup_expired_clients(android_protocol_ctx_t* ctx) {
    if (!ctx || !ctx->client_manager) {
        return -1;
    }
    
    return client_manager_auto_cleanup(ctx->client_manager);
}

// Send protocol message to bridge server
int android_send_protocol_message(android_protocol_ctx_t* ctx, message_type_t type, 
                                 uint32_t client_id, const void* data, size_t size) {
    if (!ctx) {
        LOGE("Invalid protocol context");
        return -1;
    }
    
    if (ctx->tcp_socket < 0) {
        LOGE("Bridge not connected");
        return -1;
    }
    
    if (size > PROTOCOL_MAX_PAYLOAD_SIZE) {
        LOGE("Message too large: %zu bytes", size);
        return -1;
    }
    
    // Create message buffer
    char buffer[UDP_BRIDGE_HEADER_SIZE + PROTOCOL_MAX_PAYLOAD_SIZE];
    int message_size = protocol_create_message(buffer, sizeof(buffer), type, client_id, 0, data, size);
    if (message_size < 0) {
        LOGE("Failed to create protocol message");
        return -1;
    }
    
    // Send message to bridge server
    ssize_t sent = send(ctx->tcp_socket, buffer, message_size, MSG_NOSIGNAL);
    if (sent < 0) {
        LOGE("Failed to send message to bridge: %s", strerror(errno));
        return -1;
    }
    
    if (sent != message_size) {
        LOGE("Partial send: %zd of %d bytes", sent, message_size);
        return -1;
    }
    
    LOGD("Sent %s message (%d bytes) for client %u", 
         protocol_message_type_string(type), message_size, client_id);
    return 0;
}

// Handle incoming UDP packet
int android_handle_udp_packet(android_protocol_ctx_t* ctx, const char* data, size_t size, 
                             struct sockaddr_in* from_addr) {
    if (!ctx || !data || !from_addr) {
        LOGE("Invalid parameters for UDP packet handling");
        return -1;
    }
    
    if (size == 0) {
        LOGW("Received empty UDP packet");
        return 0;
    }
    
    // Find or create client
    uint32_t client_id = android_add_or_update_client(ctx, from_addr);
    if (client_id == 0) {
        LOGE("Failed to register client");
        return -1;
    }
    
    // Forward packet to bridge if connected
    if (ctx->tcp_socket >= 0) {
        int result = android_send_protocol_message(ctx, MSG_DATA, client_id, data, size);
        if (result == 0) {
            // Update client statistics
            android_update_client_stats(ctx, client_id, size, 0);
            LOGD("Forwarded %zu bytes from client %u", size, client_id);
        }
        return result;
    } else {
        LOGW("Bridge not connected, dropping packet from client %u", client_id);
        return -1;
    }
}

// Handle protocol response from bridge server
int android_handle_protocol_response(android_protocol_ctx_t* ctx, const char* data, size_t size) {
    if (!ctx || !data || size < UDP_BRIDGE_HEADER_SIZE) {
        LOGE("Invalid parameters for protocol response");
        return -1;
    }
    
    udp_bridge_header_t header;
    if (protocol_parse_header(data, size, &header) != ERROR_NONE) {
        LOGE("Failed to parse protocol header");
        return -1;
    }
    
    if (protocol_validate_header(&header) != ERROR_NONE) {
        LOGE("Invalid protocol header");
        return -1;
    }
    
    // Extract payload
    const char* payload = data + UDP_BRIDGE_HEADER_SIZE;
    size_t payload_size = header.payload_size;
    
    // Handle different message types
    switch (header.message_type) {
        case MSG_DATA: {
            // Find client by ID
            client_entry_t* client = android_find_client_by_id(ctx, header.client_id);
            if (!client) {
                LOGW("Received data for unknown client %u", header.client_id);
                return -1;
            }
            
            // Send data to client via UDP
            if (ctx->udp_socket >= 0) {
                ssize_t sent = sendto(ctx->udp_socket, payload, payload_size, 0,
                                    (struct sockaddr*)&client->client_addr, sizeof(client->client_addr));
                if (sent < 0) {
                    LOGE("Failed to send data to client %u: %s", header.client_id, strerror(errno));
                    return -1;
                }
                
                // Update statistics
                android_update_client_stats(ctx, header.client_id, 0, payload_size);
                LOGD("Sent %zu bytes to client %u", payload_size, header.client_id);
            }
            break;
        }
        
        case MSG_PONG:
            LOGD("Received PONG from bridge");
            break;
            
        case MSG_ERROR:
            LOGW("Received ERROR from bridge: %.*s", (int)payload_size, payload);
            break;
            
        case MSG_CLIENT_TIMEOUT:
            LOGD("Client %u timed out on bridge", header.client_id);
            // Could remove client from local table
            break;
            
        default:
            LOGW("Received unknown message type: %d", header.message_type);
            break;
    }
    
    return 0;
}

int android_bridge_connect(android_protocol_ctx_t* ctx, const char* server_host, int server_port) {
    if (!ctx || !server_host || server_port <= 0) {
        LOGE("Invalid parameters for bridge connection");
        return -1;
    }
    
    LOGI("Connecting to bridge server %s:%d", server_host, server_port);
    
    // Close existing connection if any
    if (ctx->tcp_socket >= 0) {
        close(ctx->tcp_socket);
        ctx->tcp_socket = -1;
    }
    
    // Create TCP socket
    ctx->tcp_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (ctx->tcp_socket < 0) {
        LOGE("Failed to create TCP socket: %s", strerror(errno));
        return -1;
    }
    
    // Set socket timeout
    struct timeval timeout;
    timeout.tv_sec = 10;  // 10 seconds
    timeout.tv_usec = 0;
    if (setsockopt(ctx->tcp_socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0) {
        LOGW("Failed to set socket receive timeout: %s", strerror(errno));
    }
    if (setsockopt(ctx->tcp_socket, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) < 0) {
        LOGW("Failed to set socket send timeout: %s", strerror(errno));
    }
    
    // Setup server address
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(server_port);
    
    if (inet_pton(AF_INET, server_host, &server_addr.sin_addr) <= 0) {
        LOGE("Invalid server address: %s", server_host);
        close(ctx->tcp_socket);
        ctx->tcp_socket = -1;
        return -1;
    }
    
    // Connect to server
    if (connect(ctx->tcp_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        LOGE("Failed to connect to bridge server %s:%d: %s", server_host, server_port, strerror(errno));
        close(ctx->tcp_socket);
        ctx->tcp_socket = -1;
        return -1;
    }
    
    LOGI("Successfully connected to bridge server %s:%d", server_host, server_port);
    return 0;
}

// Bridge disconnection
void android_bridge_disconnect(android_protocol_ctx_t* ctx) {
    if (!ctx) return;
    
    if (ctx->tcp_socket >= 0) {
        LOGI("Disconnecting from bridge server");
        close(ctx->tcp_socket);
        ctx->tcp_socket = -1;
    }
}

// Check bridge connection status
int android_bridge_is_connected(android_protocol_ctx_t* ctx) {
    if (!ctx || ctx->tcp_socket < 0) {
        return 0;
    }
    
    // Try to send a ping to check connection
    char test_byte = 0;
    ssize_t result = send(ctx->tcp_socket, &test_byte, 0, MSG_DONTWAIT | MSG_NOSIGNAL);
    if (result < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
        LOGW("Bridge connection appears to be broken: %s", strerror(errno));
        return 0;
    }
    
    return 1;
}

// JNI function to get client statistics (useful for debugging)
JNIEXPORT jstring JNICALL Java_com_example_udpbridge_UdpBridgeProtocol_getClientStats(JNIEnv *env, jobject thiz) {
    if (!g_protocol_ctx || !g_protocol_ctx->client_manager) {
        return (*env)->NewStringUTF(env, "No client manager available");
    }
    
    uint32_t client_count = client_manager_get_count(g_protocol_ctx->client_manager);
    char stats_buffer[256];
    snprintf(stats_buffer, sizeof(stats_buffer), 
             "Active clients: %u", client_count);
    
    return (*env)->NewStringUTF(env, stats_buffer);
}

// JNI function to manually trigger client cleanup
JNIEXPORT jint JNICALL Java_com_example_udpbridge_UdpBridgeProtocol_cleanupExpiredClients(JNIEnv *env, jobject thiz) {
    if (!g_protocol_ctx || !g_protocol_ctx->client_manager) {
        return -1;
    }
    
    return android_cleanup_expired_clients(g_protocol_ctx);
}

// JNI interface implementation
JNIEXPORT jint JNICALL Java_com_example_udpbridge_UdpBridgeProtocol_initProtocol(JNIEnv *env, jobject thiz, jint local_port) {
    if (g_protocol_ctx) {
        android_protocol_cleanup(g_protocol_ctx);
        free(g_protocol_ctx);
    }
    
    g_protocol_ctx = malloc(sizeof(android_protocol_ctx_t));
    if (!g_protocol_ctx) {
        LOGE("Failed to allocate protocol context");
        return -1;
    }
    
    if (android_protocol_init(g_protocol_ctx, local_port) < 0) {
        free(g_protocol_ctx);
        g_protocol_ctx = NULL;
        return -1;
    }
    
    return 0;
}

JNIEXPORT void JNICALL Java_com_example_udpbridge_UdpBridgeProtocol_cleanupProtocol(JNIEnv *env, jobject thiz) {
    if (g_protocol_ctx) {
        android_protocol_cleanup(g_protocol_ctx);
        free(g_protocol_ctx);
        g_protocol_ctx = NULL;
    }
}

JNIEXPORT jint JNICALL Java_com_example_udpbridge_UdpBridgeProtocol_connectToBridge(JNIEnv *env, jobject thiz, jstring server_host, jint server_port) {
    if (!g_protocol_ctx) {
        LOGE("Protocol not initialized");
        return -1;
    }
    
    const char* host = (*env)->GetStringUTFChars(env, server_host, NULL);
    if (!host) {
        LOGE("Failed to get server host string");
        return -1;
    }
    
    int result = android_bridge_connect(g_protocol_ctx, host, server_port);
    
    (*env)->ReleaseStringUTFChars(env, server_host, host);
    return result;
}

JNIEXPORT void JNICALL Java_com_example_udpbridge_UdpBridgeProtocol_disconnectFromBridge(JNIEnv *env, jobject thiz) {
    if (g_protocol_ctx) {
        android_bridge_disconnect(g_protocol_ctx);
    }
}

JNIEXPORT jboolean JNICALL Java_com_example_udpbridge_UdpBridgeProtocol_isConnected(JNIEnv *env, jobject thiz) {
    return android_bridge_is_connected(g_protocol_ctx) ? JNI_TRUE : JNI_FALSE;
}
