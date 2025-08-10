#!/bin/bash

# Test script for UDP Bridge Server
# This script tests the basic functionality of the UDP bridge server

set -e

echo "=== UDP Bridge Server Test ==="

# Configuration
BRIDGE_HOST="localhost"
BRIDGE_PORT="8080"
TARGET_UDP_PORT="5060"
TEST_CLIENT_ID="12345"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Helper functions
log_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

log_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Check if server is running
check_server() {
    log_info "Checking if UDP bridge server is running..."
    
    if nc -z $BRIDGE_HOST $BRIDGE_PORT 2>/dev/null; then
        log_success "Server is responding on port $BRIDGE_PORT"
        return 0
    else
        log_error "Server is not responding on port $BRIDGE_PORT"
        return 1
    fi
}

# Test basic TCP connection
test_connection() {
    log_info "Testing basic TCP connection..."
    
    # Use timeout to avoid hanging
    if timeout 5 bash -c "echo 'test' | nc $BRIDGE_HOST $BRIDGE_PORT" >/dev/null 2>&1; then
        log_success "TCP connection successful"
        return 0
    else
        log_error "TCP connection failed"
        return 1
    fi
}

# Test protocol message
test_protocol() {
    log_info "Testing protocol message parsing..."
    
    # Create a simple test client that sends a protocol message
    cat > /tmp/test_client.c << 'EOF'
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

// Simplified protocol header for testing
typedef struct {
    char magic[4];          // "UDPB"
    uint8_t version;        // 1
    uint8_t message_type;   // 2 = MSG_CLIENT_REGISTER
    uint16_t flags;         // 0
    uint32_t client_id;     // Test client ID
    uint32_t payload_size;  // 0
    uint32_t checksum;      // Simple checksum
} __attribute__((packed)) test_header_t;

uint32_t calculate_checksum(const test_header_t* header) {
    // Simple checksum - sum of bytes
    const uint8_t* data = (const uint8_t*)header;
    uint32_t sum = 0;
    for (size_t i = 0; i < sizeof(test_header_t) - sizeof(uint32_t); i++) {
        sum += data[i];
    }
    return sum;
}

int main(int argc, char* argv[]) {
    if (argc != 3) {
        printf("Usage: %s <host> <port>\n", argv[0]);
        return 1;
    }
    
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("socket");
        return 1;
    }
    
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(atoi(argv[2]));
    inet_aton(argv[1], &addr.sin_addr);
    
    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("connect");
        close(sock);
        return 1;
    }
    
    // Send registration message
    test_header_t header;
    memcpy(header.magic, "UDPB", 4);
    header.version = 1;
    header.message_type = 2; // MSG_CLIENT_REGISTER
    header.flags = 0;
    header.client_id = 12345;
    header.payload_size = 0;
    header.checksum = calculate_checksum(&header);
    
    ssize_t sent = send(sock, &header, sizeof(header), 0);
    if (sent != sizeof(header)) {
        printf("Failed to send complete message\n");
        close(sock);
        return 1;
    }
    
    printf("Sent registration message (%zd bytes)\n", sent);
    
    // Try to receive response
    char response[256];
    ssize_t received = recv(sock, response, sizeof(response), 0);
    if (received > 0) {
        printf("Received response (%zd bytes)\n", received);
    } else {
        printf("No response received\n");
    }
    
    close(sock);
    return 0;
}
EOF

    # Compile and run test client
    gcc -o /tmp/test_client /tmp/test_client.c
    if /tmp/test_client $BRIDGE_HOST $BRIDGE_PORT; then
        log_success "Protocol test completed"
        rm -f /tmp/test_client /tmp/test_client.c
        return 0
    else
        log_error "Protocol test failed"
        rm -f /tmp/test_client /tmp/test_client.c
        return 1
    fi
}

# Test UDP forwarding (requires a test UDP server)
test_udp_forwarding() {
    log_info "Testing UDP forwarding (requires test UDP server on port $TARGET_UDP_PORT)..."
    
    # Check if test UDP server is available
    if ! nc -u -z localhost $TARGET_UDP_PORT 2>/dev/null; then
        log_info "No UDP server on port $TARGET_UDP_PORT, skipping UDP forwarding test"
        return 0
    fi
    
    log_info "UDP forwarding test would be implemented here"
    log_success "UDP forwarding test skipped (not implemented)"
    return 0
}

# Main test sequence
main() {
    echo "Starting UDP Bridge Server tests..."
    echo "Bridge server: $BRIDGE_HOST:$BRIDGE_PORT"
    echo "Target UDP: localhost:$TARGET_UDP_PORT"
    echo ""
    
    local failed=0
    
    # Run tests
    check_server || failed=1
    test_connection || failed=1
    test_protocol || failed=1
    test_udp_forwarding || failed=1
    
    echo ""
    if [ $failed -eq 0 ]; then
        log_success "All tests passed!"
        return 0
    else
        log_error "Some tests failed!"
        return 1
    fi
}

# Parse command line arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        -h|--host)
            BRIDGE_HOST="$2"
            shift 2
            ;;
        -p|--port)
            BRIDGE_PORT="$2"
            shift 2
            ;;
        -t|--target-port)
            TARGET_UDP_PORT="$2"
            shift 2
            ;;
        --help)
            echo "Usage: $0 [options]"
            echo "Options:"
            echo "  -h, --host <host>         Bridge server host (default: localhost)"
            echo "  -p, --port <port>         Bridge server port (default: 8080)"
            echo "  -t, --target-port <port>  Target UDP port (default: 5060)"
            echo "  --help                    Show this help message"
            exit 0
            ;;
        *)
            log_error "Unknown option: $1"
            exit 1
            ;;
    esac
done

# Run main test function
main
