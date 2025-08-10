#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "../src/protocol.h"

// Test colors
#define GREEN "\033[32m"
#define RED "\033[31m"
#define RESET "\033[0m"

// Test counter
static int tests_run = 0;
static int tests_passed = 0;

void test_assert(int condition, const char* test_name) {
    tests_run++;
    if (condition) {
        printf(GREEN "✓" RESET " %s\n", test_name);
        tests_passed++;
    } else {
        printf(RED "✗" RESET " %s\n", test_name);
    }
}

// Test CRC32 checksum calculation
void test_checksum() {
    const char* test_data = "Hello, World!";
    uint32_t checksum1 = protocol_calculate_checksum(test_data, strlen(test_data));
    uint32_t checksum2 = protocol_calculate_checksum(test_data, strlen(test_data));
    
    test_assert(checksum1 == checksum2, "Checksum is deterministic");
    test_assert(checksum1 != 0, "Checksum is not zero");
    
    // Test different data produces different checksum
    const char* test_data2 = "Hello, World!!";
    uint32_t checksum3 = protocol_calculate_checksum(test_data2, strlen(test_data2));
    test_assert(checksum1 != checksum3, "Different data produces different checksum");
}

// Test header validation
void test_header_validation() {
    udp_bridge_header_t header;
    
    // Valid header
    memcpy(header.magic, UDP_BRIDGE_MAGIC, 4);
    header.version = UDP_BRIDGE_VERSION;
    header.message_type = MSG_DATA;
    header.flags = FLAG_NONE;
    header.client_id = 123;
    header.payload_size = 100;
    header.checksum = 0;
    
    test_assert(protocol_validate_header(&header) == ERROR_NONE, "Valid header passes validation");
    
    // Invalid magic
    udp_bridge_header_t invalid_magic = header;
    memcpy(invalid_magic.magic, "XXXX", 4);
    test_assert(protocol_validate_header(&invalid_magic) == ERROR_INVALID_HEADER, "Invalid magic fails validation");
    
    // Invalid version
    udp_bridge_header_t invalid_version = header;
    invalid_version.version = 99;
    test_assert(protocol_validate_header(&invalid_version) == ERROR_INVALID_HEADER, "Invalid version fails validation");
    
    // Invalid message type
    udp_bridge_header_t invalid_type = header;
    invalid_type.message_type = 99;
    test_assert(protocol_validate_header(&invalid_type) == ERROR_INVALID_HEADER, "Invalid message type fails validation");
    
    // Payload too large
    udp_bridge_header_t large_payload = header;
    large_payload.payload_size = PROTOCOL_MAX_PAYLOAD_SIZE + 1;
    test_assert(protocol_validate_header(&large_payload) == ERROR_PAYLOAD_TOO_LARGE, "Large payload fails validation");
}

// Test message creation
void test_message_creation() {
    char buffer[1024];
    const char* payload = "Test payload data";
    uint32_t payload_size = strlen(payload);
    
    int result = protocol_create_message(buffer, sizeof(buffer), MSG_DATA, 
                                       123, FLAG_NONE, payload, payload_size);
    
    test_assert(result == UDP_BRIDGE_HEADER_SIZE + payload_size, "Message creation returns correct size");
    
    // Parse the created message
    udp_bridge_header_t parsed_header;
    int parse_result = protocol_parse_header(buffer, result, &parsed_header);
    
    test_assert(parse_result == ERROR_NONE, "Created message parses successfully");
    test_assert(parsed_header.message_type == MSG_DATA, "Message type is preserved");
    test_assert(parsed_header.client_id == 123, "Client ID is preserved");
    test_assert(parsed_header.payload_size == payload_size, "Payload size is preserved");
    
    // Verify payload
    const char* parsed_payload = buffer + UDP_BRIDGE_HEADER_SIZE;
    test_assert(memcmp(parsed_payload, payload, payload_size) == 0, "Payload is preserved");
}

// Test checksum validation in complete message
void test_message_checksum() {
    char buffer[1024];
    const char* payload = "Test checksum validation";
    uint32_t payload_size = strlen(payload);
    
    int msg_size = protocol_create_message(buffer, sizeof(buffer), MSG_PING, 
                                         456, FLAG_NONE, payload, payload_size);
    
    // Parse header
    udp_bridge_header_t header;
    protocol_parse_header(buffer, msg_size, &header);
    
    // Calculate checksum manually
    udp_bridge_header_t temp_header = header;
    temp_header.checksum = 0;
    
    // Create temp buffer with zero checksum
    char temp_buffer[1024];
    memcpy(temp_buffer, &temp_header, UDP_BRIDGE_HEADER_SIZE);
    memcpy(temp_buffer + UDP_BRIDGE_HEADER_SIZE, payload, payload_size);
    
    // Convert header to network byte order for checksum calculation
    udp_bridge_header_t* net_header = (udp_bridge_header_t*)temp_buffer;
    net_header->flags = htons(net_header->flags);
    net_header->client_id = htonl(net_header->client_id);
    net_header->payload_size = htonl(net_header->payload_size);
    net_header->checksum = htonl(net_header->checksum);
    
    uint32_t calculated_checksum = protocol_calculate_checksum(temp_buffer, msg_size);
    
    test_assert(header.checksum == calculated_checksum, "Message checksum is valid");
}

// Test edge cases
void test_edge_cases() {
    char buffer[1024];
    
    // Empty payload
    int result = protocol_create_message(buffer, sizeof(buffer), MSG_PING, 
                                       0, FLAG_NONE, NULL, 0);
    test_assert(result == UDP_BRIDGE_HEADER_SIZE, "Empty payload message creation works");
    
    // NULL buffer
    result = protocol_create_message(NULL, sizeof(buffer), MSG_PING, 
                                   0, FLAG_NONE, NULL, 0);
    test_assert(result == -1, "NULL buffer returns error");
    
    // Buffer too small
    char small_buffer[10];
    result = protocol_create_message(small_buffer, sizeof(small_buffer), MSG_DATA, 
                                   0, FLAG_NONE, "large payload", 13);
    test_assert(result == -1, "Small buffer returns error");
    
    // Parse NULL buffer
    udp_bridge_header_t header;
    result = protocol_parse_header(NULL, 100, &header);
    test_assert(result == -1, "Parse NULL buffer returns error");
    
    // Parse buffer too small
    result = protocol_parse_header(buffer, 10, &header);
    test_assert(result == -1, "Parse small buffer returns error");
}

// Test string functions
void test_string_functions() {
    test_assert(strcmp(protocol_message_type_string(MSG_DATA), "DATA") == 0, "Message type string for DATA");
    test_assert(strcmp(protocol_message_type_string(MSG_PING), "PING") == 0, "Message type string for PING");
    test_assert(strcmp(protocol_message_type_string(99), "UNKNOWN") == 0, "Message type string for unknown type");
    
    test_assert(strcmp(protocol_error_string(ERROR_NONE), "No error") == 0, "Error string for no error");
    test_assert(strcmp(protocol_error_string(ERROR_INVALID_HEADER), "Invalid header") == 0, "Error string for invalid header");
    test_assert(strcmp(protocol_error_string(99), "Unknown error") == 0, "Error string for unknown error");
}

// Test message validation
void test_message_validation() {
    char buffer[1024];
    const char* payload = "Test payload";
    
    // Create valid message
    int size = protocol_create_message(buffer, sizeof(buffer), MSG_DATA, 
                                     123, FLAG_NONE, payload, strlen(payload));
    test_assert(size > 0, "Message creation succeeds");
    
    // Validate complete message
    int result = protocol_validate_message(buffer, size);
    test_assert(result == ERROR_NONE, "Valid message passes validation");
    
    // Test corrupted checksum
    char corrupted[1024];
    memcpy(corrupted, buffer, size);
    corrupted[UDP_BRIDGE_HEADER_SIZE - 1] ^= 0xFF;  // Corrupt checksum
    result = protocol_validate_message(corrupted, size);
    test_assert(result == ERROR_INVALID_CHECKSUM, "Corrupted checksum detected");
    
    // Test incomplete buffer
    result = protocol_validate_message(buffer, UDP_BRIDGE_HEADER_SIZE - 1);
    test_assert(result == ERROR_INVALID_HEADER, "Incomplete buffer rejected");
}

// Test payload extraction
void test_payload_extraction() {
    char buffer[1024];
    const char* test_payload = "Extract this payload";
    uint32_t payload_size = strlen(test_payload);
    
    // Create message with payload
    int size = protocol_create_message(buffer, sizeof(buffer), MSG_DATA, 
                                     456, FLAG_NONE, test_payload, payload_size);
    test_assert(size > 0, "Message with payload created");
    
    // Extract payload
    const char* extracted_payload;
    uint32_t extracted_size;
    int result = protocol_extract_payload(buffer, size, &extracted_payload, &extracted_size);
    
    test_assert(result == ERROR_NONE, "Payload extraction succeeds");
    test_assert(extracted_size == payload_size, "Extracted size matches original");
    test_assert(memcmp(extracted_payload, test_payload, payload_size) == 0, "Extracted data matches original");
    
    // Test message without payload
    int size_no_payload = protocol_create_simple_message(buffer, sizeof(buffer), MSG_PING, 789);
    result = protocol_extract_payload(buffer, size_no_payload, &extracted_payload, &extracted_size);
    test_assert(result == ERROR_NONE, "Empty payload extraction succeeds");
    test_assert(extracted_size == 0, "Empty payload has zero size");
}

// Test message size calculation
void test_message_size() {
    udp_bridge_header_t header;
    
    header.payload_size = 0;
    test_assert(protocol_get_message_size(&header) == UDP_BRIDGE_HEADER_SIZE, "Empty message size correct");
    
    header.payload_size = 100;
    test_assert(protocol_get_message_size(&header) == UDP_BRIDGE_HEADER_SIZE + 100, "Message with payload size correct");
    
    test_assert(protocol_get_message_size(NULL) == 0, "NULL header returns zero size");
}

int main() {
    printf("Running UDP Bridge Protocol Tests\n");
    printf("=================================\n\n");
    
    test_checksum();
    test_header_validation();
    test_message_creation();
    test_message_checksum();
    test_edge_cases();
    test_string_functions();
    test_message_validation();
    test_payload_extraction();
    test_message_size();
    test_message_validation();
    test_payload_extraction();
    test_message_size();
    
    printf("\nTest Results:\n");
    printf("=============\n");
    printf("Total tests: %d\n", tests_run);
    printf("Passed: " GREEN "%d" RESET "\n", tests_passed);
    printf("Failed: " RED "%d" RESET "\n", tests_run - tests_passed);
    
    if (tests_passed == tests_run) {
        printf(GREEN "\nAll tests passed! ✓" RESET "\n");
        return 0;
    } else {
        printf(RED "\nSome tests failed! ✗" RESET "\n");
        return 1;
    }
}
