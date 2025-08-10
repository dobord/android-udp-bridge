#include <jni.h>
#include <android/log.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <errno.h>

#include "udp_bridge_protocol.h"
#include "client_manager.h"
#include "tcp_connection_manager.h"
#include "udp_listener.h"

#define LOG_TAG "UdpBridgeJNI"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)

// Global state
static struct {
    int udp_socket;
    int tcp_socket;
    struct sockaddr_in bridge_addr;
    client_manager_t* client_manager;
    tcp_connection_manager_t* tcp_manager;
    pthread_t bridge_thread;
    pthread_t listener_thread;
    int running;
    int local_port;
    char bridge_host[256];
    int bridge_port;
    long bytes_transferred;
    int protocol_connected;
    pthread_mutex_t state_mutex;
} bridge_state = {
    .udp_socket = -1,
    .tcp_socket = -1,
    .client_manager = NULL,
    .tcp_manager = NULL,
    .running = 0,
    .local_port = 0,
    .bridge_host = {0},
    .bridge_port = 0,
    .bytes_transferred = 0,
    .protocol_connected = 0,
    .state_mutex = PTHREAD_MUTEX_INITIALIZER
};

// Forward declarations
void* bridge_thread_func(void* arg);
void* listener_thread_func(void* arg);

JNIEXPORT jboolean JNICALL
Java_com_example_udpbridge_UdpBridgeService_initializeBridge(JNIEnv *env, jobject thiz, jint localPort) {
    LOGI("Initializing UDP Bridge on port %d", localPort);
    
    pthread_mutex_lock(&bridge_state.state_mutex);
    
    // Clean up any existing state
    if (bridge_state.running) {
        LOGE("Bridge already running, stopping first");
        bridge_state.running = 0;
        pthread_mutex_unlock(&bridge_state.state_mutex);
        return JNI_FALSE;
    }
    
    bridge_state.local_port = localPort;
    
    // Create UDP socket
    bridge_state.udp_socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (bridge_state.udp_socket < 0) {
        LOGE("Failed to create UDP socket: %s", strerror(errno));
        pthread_mutex_unlock(&bridge_state.state_mutex);
        return JNI_FALSE;
    }
    
    // Set socket options
    int reuse = 1;
    if (setsockopt(bridge_state.udp_socket, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
        LOGE("Failed to set SO_REUSEADDR: %s", strerror(errno));
    }
    
    // Bind UDP socket
    struct sockaddr_in local_addr = {0};
    local_addr.sin_family = AF_INET;
    local_addr.sin_addr.s_addr = INADDR_ANY;
    local_addr.sin_port = htons(localPort);
    
    if (bind(bridge_state.udp_socket, (struct sockaddr*)&local_addr, sizeof(local_addr)) < 0) {
        LOGE("Failed to bind UDP socket to port %d: %s", localPort, strerror(errno));
        close(bridge_state.udp_socket);
        bridge_state.udp_socket = -1;
        pthread_mutex_unlock(&bridge_state.state_mutex);
        return JNI_FALSE;
    }
    
    // Initialize client manager
    bridge_state.client_manager = client_manager_create();
    if (!bridge_state.client_manager) {
        LOGE("Failed to create client manager");
        close(bridge_state.udp_socket);
        bridge_state.udp_socket = -1;
        pthread_mutex_unlock(&bridge_state.state_mutex);
        return JNI_FALSE;
    }
    
    bridge_state.bytes_transferred = 0;
    
    LOGI("UDP Bridge initialized successfully on port %d", localPort);
    pthread_mutex_unlock(&bridge_state.state_mutex);
    return JNI_TRUE;
}

JNIEXPORT jboolean JNICALL
Java_com_example_udpbridge_UdpBridgeService_connectToBridgeServer(JNIEnv *env, jobject thiz, jstring host, jint port) {
    const char* host_str = (*env)->GetStringUTFChars(env, host, NULL);
    if (!host_str) {
        LOGE("Failed to get host string");
        return JNI_FALSE;
    }
    
    LOGI("Connecting to bridge server %s:%d", host_str, port);
    
    pthread_mutex_lock(&bridge_state.state_mutex);
    
    // Store connection details
    strncpy(bridge_state.bridge_host, host_str, sizeof(bridge_state.bridge_host) - 1);
    bridge_state.bridge_port = port;
    
    // Create TCP socket
    bridge_state.tcp_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (bridge_state.tcp_socket < 0) {
        LOGE("Failed to create TCP socket: %s", strerror(errno));
        (*env)->ReleaseStringUTFChars(env, host, host_str);
        pthread_mutex_unlock(&bridge_state.state_mutex);
        return JNI_FALSE;
    }
    
    // Set up bridge server address
    memset(&bridge_state.bridge_addr, 0, sizeof(bridge_state.bridge_addr));
    bridge_state.bridge_addr.sin_family = AF_INET;
    bridge_state.bridge_addr.sin_port = htons(port);
    
    if (inet_pton(AF_INET, host_str, &bridge_state.bridge_addr.sin_addr) <= 0) {
        LOGE("Invalid bridge server address: %s", host_str);
        close(bridge_state.tcp_socket);
        bridge_state.tcp_socket = -1;
        (*env)->ReleaseStringUTFChars(env, host, host_str);
        pthread_mutex_unlock(&bridge_state.state_mutex);
        return JNI_FALSE;
    }
    
    // Connect to bridge server
    if (connect(bridge_state.tcp_socket, (struct sockaddr*)&bridge_state.bridge_addr, 
                sizeof(bridge_state.bridge_addr)) < 0) {
        LOGE("Failed to connect to bridge server %s:%d: %s", host_str, port, strerror(errno));
        close(bridge_state.tcp_socket);
        bridge_state.tcp_socket = -1;
        (*env)->ReleaseStringUTFChars(env, host, host_str);
        pthread_mutex_unlock(&bridge_state.state_mutex);
        return JNI_FALSE;
    }
    
    // Initialize TCP connection manager
    bridge_state.tcp_manager = tcp_connection_manager_create(bridge_state.tcp_socket);
    if (!bridge_state.tcp_manager) {
        LOGE("Failed to create TCP connection manager");
        close(bridge_state.tcp_socket);
        bridge_state.tcp_socket = -1;
        (*env)->ReleaseStringUTFChars(env, host, host_str);
        pthread_mutex_unlock(&bridge_state.state_mutex);
        return JNI_FALSE;
    }
    
    bridge_state.protocol_connected = 1;
    
    LOGI("Connected to bridge server %s:%d", host_str, port);
    (*env)->ReleaseStringUTFChars(env, host, host_str);
    pthread_mutex_unlock(&bridge_state.state_mutex);
    return JNI_TRUE;
}

JNIEXPORT jboolean JNICALL
Java_com_example_udpbridge_UdpBridgeService_startProtocolHandler(JNIEnv *env, jobject thiz) {
    LOGI("Starting protocol handler");
    
    pthread_mutex_lock(&bridge_state.state_mutex);
    
    if (bridge_state.running) {
        LOGE("Protocol handler already running");
        pthread_mutex_unlock(&bridge_state.state_mutex);
        return JNI_FALSE;
    }
    
    if (bridge_state.udp_socket < 0 || bridge_state.tcp_socket < 0) {
        LOGE("Sockets not initialized");
        pthread_mutex_unlock(&bridge_state.state_mutex);
        return JNI_FALSE;
    }
    
    bridge_state.running = 1;
    
    // Start bridge thread (handles TCP communication with server)
    if (pthread_create(&bridge_state.bridge_thread, NULL, bridge_thread_func, NULL) != 0) {
        LOGE("Failed to create bridge thread");
        bridge_state.running = 0;
        pthread_mutex_unlock(&bridge_state.state_mutex);
        return JNI_FALSE;
    }
    
    // Start listener thread (handles UDP packets from clients)
    if (pthread_create(&bridge_state.listener_thread, NULL, listener_thread_func, NULL) != 0) {
        LOGE("Failed to create listener thread");
        bridge_state.running = 0;
        pthread_cancel(bridge_state.bridge_thread);
        pthread_mutex_unlock(&bridge_state.state_mutex);
        return JNI_FALSE;
    }
    
    LOGI("Protocol handler started successfully");
    pthread_mutex_unlock(&bridge_state.state_mutex);
    return JNI_TRUE;
}

JNIEXPORT void JNICALL
Java_com_example_udpbridge_UdpBridgeService_stopBridge(JNIEnv *env, jobject thiz) {
    LOGI("Stopping UDP Bridge");
    
    pthread_mutex_lock(&bridge_state.state_mutex);
    
    bridge_state.running = 0;
    bridge_state.protocol_connected = 0;
    
    // Cancel threads
    if (bridge_state.bridge_thread) {
        pthread_cancel(bridge_state.bridge_thread);
        pthread_join(bridge_state.bridge_thread, NULL);
        bridge_state.bridge_thread = 0;
    }
    
    if (bridge_state.listener_thread) {
        pthread_cancel(bridge_state.listener_thread);
        pthread_join(bridge_state.listener_thread, NULL);
        bridge_state.listener_thread = 0;
    }
    
    // Cleanup TCP manager
    if (bridge_state.tcp_manager) {
        tcp_connection_manager_destroy(bridge_state.tcp_manager);
        bridge_state.tcp_manager = NULL;
    }
    
    // Close sockets
    if (bridge_state.tcp_socket >= 0) {
        close(bridge_state.tcp_socket);
        bridge_state.tcp_socket = -1;
    }
    
    if (bridge_state.udp_socket >= 0) {
        close(bridge_state.udp_socket);
        bridge_state.udp_socket = -1;
    }
    
    // Cleanup client manager
    if (bridge_state.client_manager) {
        client_manager_destroy(bridge_state.client_manager);
        bridge_state.client_manager = NULL;
    }
    
    bridge_state.bytes_transferred = 0;
    
    LOGI("UDP Bridge stopped");
    pthread_mutex_unlock(&bridge_state.state_mutex);
}

JNIEXPORT jint JNICALL
Java_com_example_udpbridge_UdpBridgeService_getClientCount(JNIEnv *env, jobject thiz) {
    pthread_mutex_lock(&bridge_state.state_mutex);
    int count = bridge_state.client_manager ? 
                client_manager_get_client_count(bridge_state.client_manager) : 0;
    pthread_mutex_unlock(&bridge_state.state_mutex);
    return count;
}

JNIEXPORT jlong JNICALL
Java_com_example_udpbridge_UdpBridgeService_getBytesTransferred(JNIEnv *env, jobject thiz) {
    pthread_mutex_lock(&bridge_state.state_mutex);
    long bytes = bridge_state.bytes_transferred;
    pthread_mutex_unlock(&bridge_state.state_mutex);
    return bytes;
}

JNIEXPORT jboolean JNICALL
Java_com_example_udpbridge_UdpBridgeService_isProtocolConnected(JNIEnv *env, jobject thiz) {
    pthread_mutex_lock(&bridge_state.state_mutex);
    int connected = bridge_state.protocol_connected && bridge_state.running;
    pthread_mutex_unlock(&bridge_state.state_mutex);
    return connected ? JNI_TRUE : JNI_FALSE;
}

// Thread function for handling TCP communication with bridge server
void* bridge_thread_func(void* arg) {
    LOGI("Bridge thread started");
    
    char buffer[4096];
    
    while (bridge_state.running) {
        // Receive data from bridge server
        ssize_t bytes_received = recv(bridge_state.tcp_socket, buffer, sizeof(buffer), 0);
        
        if (bytes_received <= 0) {
            if (bytes_received == 0) {
                LOGI("Bridge server disconnected");
            } else {
                LOGE("Error receiving from bridge server: %s", strerror(errno));
            }
            bridge_state.protocol_connected = 0;
            break;
        }
        
        LOGD("Received %zd bytes from bridge server", bytes_received);
        
        // Parse protocol message
        udp_bridge_header_t header;
        if (parse_header(buffer, &header) == 0) {
            // Handle different message types
            switch (header.message_type) {
                case MSG_DATA: {
                    // Forward data to appropriate UDP client
                    if (bridge_state.client_manager) {
                        client_info_t* client = client_manager_find_by_id(
                            bridge_state.client_manager, header.client_id);
                        
                        if (client) {
                            // Send UDP packet to client
                            ssize_t sent = sendto(bridge_state.udp_socket, 
                                                buffer + sizeof(udp_bridge_header_t),
                                                header.payload_size,
                                                0,
                                                (struct sockaddr*)&client->addr,
                                                sizeof(client->addr));
                            
                            if (sent > 0) {
                                bridge_state.bytes_transferred += sent;
                                client_manager_update_activity(bridge_state.client_manager, 
                                                             header.client_id);
                            }
                        }
                    }
                    break;
                }
                
                case MSG_CLIENT_TIMEOUT:
                    // Remove timed out client
                    if (bridge_state.client_manager) {
                        client_manager_remove_client(bridge_state.client_manager, 
                                                   header.client_id);
                    }
                    break;
                    
                case MSG_PONG:
                    // Keep alive response
                    LOGD("Received PONG from bridge server");
                    break;
                    
                default:
                    LOGD("Unknown message type: %d", header.message_type);
                    break;
            }
        }
    }
    
    LOGI("Bridge thread exiting");
    return NULL;
}

// Thread function for handling UDP packets from clients
void* listener_thread_func(void* arg) {
    LOGI("Listener thread started");
    
    char buffer[4096];
    struct sockaddr_in client_addr;
    socklen_t addr_len;
    
    while (bridge_state.running) {
        addr_len = sizeof(client_addr);
        
        // Receive UDP packet from client
        ssize_t bytes_received = recvfrom(bridge_state.udp_socket, buffer, sizeof(buffer), 0,
                                        (struct sockaddr*)&client_addr, &addr_len);
        
        if (bytes_received <= 0) {
            if (errno != EINTR && bridge_state.running) {
                LOGE("Error receiving UDP packet: %s", strerror(errno));
            }
            continue;
        }
        
        LOGD("Received %zd bytes from UDP client", bytes_received);
        
        // Find or create client
        uint32_t client_id = 0;
        if (bridge_state.client_manager) {
            client_info_t* client = client_manager_find_or_create(
                bridge_state.client_manager, &client_addr);
            
            if (client) {
                client_id = client->client_id;
                client_manager_update_activity(bridge_state.client_manager, client_id);
            }
        }
        
        // Send to bridge server via TCP with protocol wrapper
        if (bridge_state.tcp_manager && client_id > 0) {
            char protocol_buffer[4096 + sizeof(udp_bridge_header_t)];
            
            int message_size = create_message(protocol_buffer, MSG_DATA, client_id, 
                                            buffer, bytes_received);
            
            if (message_size > 0) {
                if (tcp_connection_manager_send(bridge_state.tcp_manager, 
                                              protocol_buffer, message_size) > 0) {
                    bridge_state.bytes_transferred += bytes_received;
                    LOGD("Forwarded %zd bytes to bridge server for client %u", 
                         bytes_received, client_id);
                }
            }
        }
    }
    
    LOGI("Listener thread exiting");
    return NULL;
}
