#include "tcp_connection_manager.h"
#include "tcp_udp_bridge.h"
#include "protocol_common.h"
#include <stdio.h>
#include <unistd.h>
#include <assert.h>
#include <string.h>

// Mock Android logging functions
int __android_log_print(int prio, const char *tag, const char *fmt, ...) {
    (void)prio; (void)tag; (void)fmt;
    return 0;
}

// Mock protocol functions for testing
int protocol_create_message(char* buffer, size_t buffer_size, message_type_t type, 
                           uint32_t client_id, uint16_t flags, const void* payload, uint32_t payload_size) {
    if (!buffer || buffer_size < UDP_BRIDGE_HEADER_SIZE) return -1;
    
    udp_bridge_header_t* header = (udp_bridge_header_t*)buffer;
    memcpy(header->magic, UDP_BRIDGE_MAGIC, 4);
    header->version = UDP_BRIDGE_VERSION;
    header->message_type = type;
    header->flags = flags;
    header->client_id = client_id;
    header->payload_size = payload_size;
    header->checksum = 0; // Mock checksum
    
    if (payload && payload_size > 0 && buffer_size >= UDP_BRIDGE_HEADER_SIZE + payload_size) {
        memcpy(buffer + UDP_BRIDGE_HEADER_SIZE, payload, payload_size);
    }
    
    return UDP_BRIDGE_HEADER_SIZE + payload_size;
}

int protocol_parse_header(const char* buffer, size_t buffer_size, udp_bridge_header_t* header) {
    if (!buffer || !header || buffer_size < UDP_BRIDGE_HEADER_SIZE) return -1;
    
    memcpy(header, buffer, UDP_BRIDGE_HEADER_SIZE);
    return 0;
}

int protocol_validate_header(const udp_bridge_header_t* header) {
    if (!header) return -1;
    if (memcmp(header->magic, UDP_BRIDGE_MAGIC, 4) != 0) return -1;
    if (header->version != UDP_BRIDGE_VERSION) return -1;
    return 0;
}

const char* protocol_message_type_string(message_type_t type) {
    switch(type) {
        case MSG_DATA: return "DATA";
        case MSG_PING: return "PING";
        case MSG_PONG: return "PONG";
        case MSG_ERROR: return "ERROR";
        case MSG_CLIENT_REGISTER: return "CLIENT_REGISTER";
        case MSG_CLIENT_TIMEOUT: return "CLIENT_TIMEOUT";
        default: return "UNKNOWN";
    }
}

// Mock UDP Bridge protocol functions
int android_handle_protocol_response(android_protocol_ctx_t* ctx, const char* data, size_t size) {
    (void)ctx; (void)data; (void)size;
    return 0;
}

static void test_tcp_connection_manager_creation() {
    printf("Testing TCP Connection Manager creation...\n");
    
    tcp_connection_manager_t* manager = tcp_connection_manager_create();
    assert(manager != NULL);
    assert(tcp_connection_manager_get_state(manager) == TCP_CONN_DISCONNECTED);
    assert(!tcp_connection_manager_is_connected(manager));
    
    tcp_connection_manager_destroy(manager);
    printf("✓ TCP Connection Manager creation test passed\n");
}

static void test_tcp_connection_manager_config() {
    printf("Testing TCP Connection Manager configuration...\n");
    
    tcp_connection_manager_t* manager = tcp_connection_manager_create();
    assert(manager != NULL);
    
    // Test reconnection configuration
    tcp_reconnect_config_t config;
    config.enabled = 1;
    config.max_attempts = 5;
    config.initial_delay_ms = 500;
    config.max_delay_ms = 10000;
    config.backoff_multiplier = 1.5f;
    config.jitter_ms = 100;
    
    tcp_connection_manager_configure_reconnect(manager, &config);
    
    // Test enabling/disabling reconnect
    assert(tcp_connection_manager_enable_reconnect(manager, 0) == 0);
    assert(tcp_connection_manager_enable_reconnect(manager, 1) == 0);
    
    tcp_connection_manager_destroy(manager);
    printf("✓ TCP Connection Manager configuration test passed\n");
}

static void test_tcp_connection_manager_stats() {
    printf("Testing TCP Connection Manager statistics...\n");
    
    tcp_connection_manager_t* manager = tcp_connection_manager_create();
    assert(manager != NULL);
    
    tcp_connection_stats_t stats = tcp_connection_manager_get_stats(manager);
    assert(stats.bytes_sent == 0);
    assert(stats.bytes_received == 0);
    assert(stats.messages_sent == 0);
    assert(stats.messages_received == 0);
    assert(stats.reconnect_count == 0);
    
    tcp_connection_manager_reset_stats(manager);
    
    tcp_connection_manager_destroy(manager);
    printf("✓ TCP Connection Manager statistics test passed\n");
}

static void test_tcp_udp_bridge_creation() {
    printf("Testing TCP-UDP Bridge creation...\n");
    
    tcp_udp_bridge_t* bridge = tcp_udp_bridge_create();
    assert(bridge != NULL);
    assert(!tcp_udp_bridge_is_active(bridge));
    
    int result = tcp_udp_bridge_configure(bridge, "localhost", 8080, 5060);
    assert(result == 0);
    
    tcp_udp_bridge_destroy(bridge);
    printf("✓ TCP-UDP Bridge creation test passed\n");
}

static void test_tcp_udp_bridge_stats() {
    printf("Testing TCP-UDP Bridge statistics...\n");
    
    tcp_udp_bridge_t* bridge = tcp_udp_bridge_create();
    assert(bridge != NULL);
    
    uint64_t udp_to_tcp_packets, tcp_to_udp_packets;
    uint64_t udp_to_tcp_bytes, tcp_to_udp_bytes;
    uint32_t errors;
    
    tcp_udp_bridge_get_stats(bridge, &udp_to_tcp_packets, &tcp_to_udp_packets,
                             &udp_to_tcp_bytes, &tcp_to_udp_bytes, &errors);
    
    assert(udp_to_tcp_packets == 0);
    assert(tcp_to_udp_packets == 0);
    assert(udp_to_tcp_bytes == 0);
    assert(tcp_to_udp_bytes == 0);
    assert(errors == 0);
    
    tcp_udp_bridge_reset_stats(bridge);
    
    tcp_udp_bridge_destroy(bridge);
    printf("✓ TCP-UDP Bridge statistics test passed\n");
}

static void test_protocol_integration() {
    printf("Testing protocol integration...\n");
    
    tcp_connection_manager_t* manager = tcp_connection_manager_create();
    assert(manager != NULL);
    
    // Test message creation
    char buffer[1024];
    int size = protocol_create_message(buffer, sizeof(buffer), MSG_PING, 123, FLAG_NONE, NULL, 0);
    assert(size > 0);
    assert(size == UDP_BRIDGE_HEADER_SIZE); // No payload for PING
    
    // Test header parsing
    udp_bridge_header_t header;
    int parse_result = protocol_parse_header(buffer, size, &header);
    assert(parse_result == 0);
    assert(header.message_type == MSG_PING);
    assert(header.client_id == 123);
    assert(header.payload_size == 0);
    
    // Test header validation
    assert(protocol_validate_header(&header) == 0);
    
    tcp_connection_manager_destroy(manager);
    printf("✓ Protocol integration test passed\n");
}

static void test_connection_state_strings() {
    printf("Testing connection state strings...\n");
    
    assert(strcmp(tcp_connection_state_string(TCP_CONN_DISCONNECTED), "DISCONNECTED") == 0);
    assert(strcmp(tcp_connection_state_string(TCP_CONN_CONNECTING), "CONNECTING") == 0);
    assert(strcmp(tcp_connection_state_string(TCP_CONN_CONNECTED), "CONNECTED") == 0);
    assert(strcmp(tcp_connection_state_string(TCP_CONN_RECONNECTING), "RECONNECTING") == 0);
    assert(strcmp(tcp_connection_state_string(TCP_CONN_ERROR), "ERROR") == 0);
    
    printf("✓ Connection state strings test passed\n");
}

static void test_error_handling() {
    printf("Testing error handling...\n");
    
    tcp_connection_manager_t* manager = tcp_connection_manager_create();
    assert(manager != NULL);
    
    // Test invalid parameters
    assert(tcp_connection_manager_connect(NULL, "localhost", 8080) == -1);
    assert(tcp_connection_manager_connect(manager, NULL, 8080) == -1);
    
    // Test sending without connection
    assert(tcp_connection_manager_send_ping(manager) == -1);
    assert(tcp_connection_manager_send_data(manager, 123, "test", 4) == -1);
    
    tcp_connection_manager_destroy(manager);
    printf("✓ Error handling test passed\n");
}

int main() {
    printf("Starting TCP Connection Manager tests...\n\n");
    
    test_tcp_connection_manager_creation();
    test_tcp_connection_manager_config();
    test_tcp_connection_manager_stats();
    test_tcp_udp_bridge_creation();
    test_tcp_udp_bridge_stats();
    test_protocol_integration();
    test_connection_state_strings();
    test_error_handling();
    
    printf("\n✅ All TCP Connection Manager tests passed!\n");
    return 0;
}
