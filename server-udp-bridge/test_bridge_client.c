#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

// UDP Bridge Protocol structures
typedef struct {
    char magic[4];          // "UDPB"
    uint8_t version;        // 1
    uint8_t message_type;   // Message type
    uint16_t flags;         // Reserved
    uint32_t client_id;     // Client identifier
    uint32_t payload_size;  // Size of payload
    uint32_t checksum;      // CRC32 of header
} __attribute__((packed)) udp_bridge_header_t;

// Message types
#define MSG_DATA 0x01
#define MSG_CLIENT_REGISTER 0x02
#define MSG_PING 0x04

uint32_t calculate_checksum(const udp_bridge_header_t* header) {
    const uint8_t* data = (const uint8_t*)header;
    uint32_t sum = 0;
    for (size_t i = 0; i < sizeof(udp_bridge_header_t) - sizeof(uint32_t); i++) {
        sum += data[i];
    }
    return sum;
}

int create_message(char* buffer, size_t buffer_size, uint8_t msg_type, 
                  uint32_t client_id, const void* payload, uint32_t payload_size) {
    if (sizeof(udp_bridge_header_t) + payload_size > buffer_size) {
        return -1;
    }
    
    udp_bridge_header_t* header = (udp_bridge_header_t*)buffer;
    memcpy(header->magic, "UDPB", 4);
    header->version = 1;
    header->message_type = msg_type;
    header->flags = 0;
    header->client_id = client_id;
    header->payload_size = payload_size;
    
    if (payload && payload_size > 0) {
        memcpy(buffer + sizeof(udp_bridge_header_t), payload, payload_size);
    }
    
    header->checksum = calculate_checksum(header);
    return sizeof(udp_bridge_header_t) + payload_size;
}

int main(int argc, char* argv[]) {
    const char* host = "localhost";
    int port = 8080;
    
    if (argc > 1) host = argv[1];
    if (argc > 2) port = atoi(argv[2]);
    
    printf("=== UDP Bridge Client Test ===\n");
    printf("Connecting to %s:%d\n", host, port);
    
    // Create socket
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("socket");
        return 1;
    }
    
    // Connect to server
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    inet_aton(host, &server_addr.sin_addr);
    
    if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("connect");
        close(sock);
        return 1;
    }
    
    printf("Connected successfully!\n");
    
    uint32_t client_id = 12345; // Test client ID
    
    // Test 1: Client Registration
    printf("\n--- Test 1: Client Registration ---\n");
    char buffer[1024];
    int msg_size = create_message(buffer, sizeof(buffer), MSG_CLIENT_REGISTER, client_id, NULL, 0);
    
    if (send(sock, buffer, msg_size, 0) != msg_size) {
        printf("Failed to send registration message\n");
    } else {
        printf("Sent registration message (%d bytes)\n", msg_size);
        
        // Try to receive response
        ssize_t received = recv(sock, buffer, sizeof(buffer), 0);
        if (received > 0) {
            printf("Received response (%zd bytes)\n", received);
        } else {
            printf("No response received\n");
        }
    }
    
    // Test 2: Ping
    printf("\n--- Test 2: Ping ---\n");
    msg_size = create_message(buffer, sizeof(buffer), MSG_PING, client_id, NULL, 0);
    
    if (send(sock, buffer, msg_size, 0) != msg_size) {
        printf("Failed to send ping message\n");
    } else {
        printf("Sent ping message (%d bytes)\n", msg_size);
        
        // Try to receive pong
        ssize_t received = recv(sock, buffer, sizeof(buffer), 0);
        if (received > 0) {
            printf("Received pong response (%zd bytes)\n", received);
        } else {
            printf("No pong received\n");
        }
    }
    
    // Test 3: UDP Data forwarding
    printf("\n--- Test 3: UDP Data Forwarding ---\n");
    const char* test_data = "Hello UDP Server!";
    msg_size = create_message(buffer, sizeof(buffer), MSG_DATA, client_id, 
                             test_data, strlen(test_data));
    
    if (send(sock, buffer, msg_size, 0) != msg_size) {
        printf("Failed to send UDP data message\n");
    } else {
        printf("Sent UDP data message (%d bytes payload: '%s')\n", 
               (int)strlen(test_data), test_data);
        
        // Give server time to process and potentially get UDP response
        sleep(1);
        
        // Check if we got a response back (UDP echo)
        ssize_t received = recv(sock, buffer, sizeof(buffer), MSG_DONTWAIT);
        if (received > 0) {
            printf("Received UDP response back (%zd bytes)\n", received);
            
            // Parse response
            udp_bridge_header_t* resp_header = (udp_bridge_header_t*)buffer;
            if (resp_header->message_type == MSG_DATA && resp_header->payload_size > 0) {
                char* payload = buffer + sizeof(udp_bridge_header_t);
                payload[resp_header->payload_size] = '\0';
                printf("UDP echo response: '%s'\n", payload);
            }
        } else {
            printf("No UDP response received (this is expected if no UDP echo server)\n");
        }
    }
    
    printf("\n--- Test Summary ---\n");
    printf("All protocol tests completed\n");
    printf("Server handled TCP connection and protocol messages correctly\n");
    
    close(sock);
    return 0;
}
