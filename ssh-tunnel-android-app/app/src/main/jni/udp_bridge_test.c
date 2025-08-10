#include "udp_bridge_protocol.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

// Simple test function to verify protocol implementation
int test_protocol_basic() {
    // Test header creation and parsing
    char buffer[1024];
    udp_bridge_header_t header;
    
    // Create a simple ping message
    int size = protocol_create_message(buffer, sizeof(buffer), MSG_PING, 12345, FLAG_NONE, NULL, 0);
    if (size < 0) {
        LOGE("Failed to create message");
        return -1;
    }
    
    // Parse the header back
    if (protocol_parse_header(buffer, size, &header) != ERROR_NONE) {
        LOGE("Failed to parse header");
        return -1;
    }
    
    // Validate header
    if (protocol_validate_header(&header) != ERROR_NONE) {
        LOGE("Header validation failed");
        return -1;
    }
    
    // Check values
    if (header.message_type != MSG_PING || header.client_id != 12345) {
        LOGE("Header values mismatch");
        return -1;
    }
    
    LOGI("Protocol basic test passed");
    return 0;
}

// Test client management
int test_client_management() {
    android_protocol_ctx_t ctx;
    
    if (android_protocol_init(&ctx, 5060) < 0) {
        LOGE("Failed to initialize protocol context");
        return -1;
    }
    
    // Create test client address
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    addr.sin_port = htons(12345);
    
    // Find or create client
    android_client_t* client = android_find_or_create_client(&ctx, &addr);
    if (!client) {
        LOGE("Failed to create client");
        android_protocol_cleanup(&ctx);
        return -1;
    }
    
    LOGI("Created client with ID: %d", client->client_id);
    
    // Find the same client again
    android_client_t* client2 = android_find_or_create_client(&ctx, &addr);
    if (client != client2) {
        LOGE("Client lookup failed");
        android_protocol_cleanup(&ctx);
        return -1;
    }
    
    android_protocol_cleanup(&ctx);
    LOGI("Client management test passed");
    return 0;
}

// Entry point for testing
JNIEXPORT jint JNICALL Java_com_example_udpbridge_UdpBridgeProtocol_runSelfTest(JNIEnv *env, jobject thiz) {
    LOGI("Running UDP Bridge Protocol self-tests");
    
    if (test_protocol_basic() < 0) {
        LOGE("Basic protocol test failed");
        return -1;
    }
    
    if (test_client_management() < 0) {
        LOGE("Client management test failed");
        return -1;
    }
    
    LOGI("All self-tests passed");
    return 0;
}
