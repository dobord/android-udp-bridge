#ifndef ANDROID_UDP_BRIDGE_PROTOCOL_H
#define ANDROID_UDP_BRIDGE_PROTOCOL_H

#include "protocol_common.h"
#include "client_manager.h"
#include <jni.h>
#include <android/log.h>

// Android logging macro
#define LOG_TAG "UdpBridgeProtocol"
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

// Protocol context for Android (updated to use client_manager)
typedef struct {
    client_manager_t* client_manager;   // Client management
    int tcp_socket;                     // Connection to bridge server
    int udp_socket;                     // Local UDP socket
    int local_port;                     // Local UDP port
    pthread_mutex_t mutex;              // Thread safety for socket operations
} android_protocol_ctx_t;

// Core protocol functions
int protocol_parse_header(const char* buffer, size_t buffer_size, udp_bridge_header_t* header);
int protocol_validate_header(const udp_bridge_header_t* header);
int protocol_create_message(char* buffer, size_t buffer_size, message_type_t type, 
                           uint32_t client_id, uint16_t flags, const void* payload, uint32_t payload_size);
uint32_t protocol_calculate_checksum(const void* data, size_t size);
const char* protocol_message_type_string(message_type_t type);
const char* protocol_error_string(uint32_t error_code);

// Android specific functions (updated for client_manager)
int android_protocol_init(android_protocol_ctx_t* ctx, int local_port);
void android_protocol_cleanup(android_protocol_ctx_t* ctx);
uint32_t android_add_or_update_client(android_protocol_ctx_t* ctx, struct sockaddr_in* addr);
client_entry_t* android_find_client_by_id(android_protocol_ctx_t* ctx, uint32_t client_id);
client_entry_t* android_find_client_by_addr(android_protocol_ctx_t* ctx, struct sockaddr_in* addr);
int android_update_client_stats(android_protocol_ctx_t* ctx, uint32_t client_id, 
                               uint32_t bytes_received, uint32_t bytes_sent);
int android_cleanup_expired_clients(android_protocol_ctx_t* ctx);
int android_send_protocol_message(android_protocol_ctx_t* ctx, message_type_t type, 
                                 uint32_t client_id, const void* data, size_t size);
int android_handle_udp_packet(android_protocol_ctx_t* ctx, const char* data, size_t size, 
                             struct sockaddr_in* from_addr);
int android_handle_protocol_response(android_protocol_ctx_t* ctx, const char* data, size_t size);

// Bridge connection management
int android_bridge_connect(android_protocol_ctx_t* ctx, const char* server_host, int server_port);
void android_bridge_disconnect(android_protocol_ctx_t* ctx);
int android_bridge_is_connected(android_protocol_ctx_t* ctx);

// JNI interface functions
JNIEXPORT jint JNICALL Java_com_example_udpbridge_UdpBridgeProtocol_initProtocol(JNIEnv *env, jobject thiz, jint local_port);
JNIEXPORT void JNICALL Java_com_example_udpbridge_UdpBridgeProtocol_cleanupProtocol(JNIEnv *env, jobject thiz);
JNIEXPORT jint JNICALL Java_com_example_udpbridge_UdpBridgeProtocol_connectToBridge(JNIEnv *env, jobject thiz, jstring server_host, jint server_port);
JNIEXPORT void JNICALL Java_com_example_udpbridge_UdpBridgeProtocol_disconnectFromBridge(JNIEnv *env, jobject thiz);
JNIEXPORT jboolean JNICALL Java_com_example_udpbridge_UdpBridgeProtocol_isConnected(JNIEnv *env, jobject thiz);
JNIEXPORT jstring JNICALL Java_com_example_udpbridge_UdpBridgeProtocol_getClientStats(JNIEnv *env, jobject thiz);
JNIEXPORT jint JNICALL Java_com_example_udpbridge_UdpBridgeProtocol_cleanupExpiredClients(JNIEnv *env, jobject thiz);

#endif // ANDROID_UDP_BRIDGE_PROTOCOL_H
