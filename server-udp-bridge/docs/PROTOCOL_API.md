# UDP Bridge Protocol API Documentation

## Overview

The UDP Bridge Protocol is a custom binary protocol designed for multiplexing UDP traffic over SSH tunnels. It provides reliable transmission of UDP packets between Android clients and server-side UDP services.

## Protocol Structure

### Header Format

All messages begin with a 20-byte header:

```c
typedef struct {
    char magic[4];          // "UDPB" - Protocol identifier
    uint8_t version;        // Protocol version (currently 1)
    uint8_t message_type;   // Type of message (see Message Types)
    uint16_t flags;         // Protocol flags (see Flags)
    uint32_t client_id;     // Unique client identifier
    uint32_t payload_size;  // Size of payload data in bytes
    uint32_t checksum;      // CRC32 checksum of entire message
} __attribute__((packed)) udp_bridge_header_t;
```

**Field Descriptions:**
- `magic`: Always "UDPB" to identify protocol messages
- `version`: Protocol version number (1)
- `message_type`: Type of message (see below)
- `flags`: Protocol-specific flags (reserved for future use)
- `client_id`: Unique identifier for UDP client (0 for new registrations)
- `payload_size`: Number of bytes following the header
- `checksum`: CRC32 checksum of header + payload

### Message Types

```c
typedef enum {
    MSG_DATA = 0x01,            // UDP packet data
    MSG_CLIENT_REGISTER = 0x02, // Client registration request/response
    MSG_CLIENT_TIMEOUT = 0x03,  // Client timeout notification
    MSG_PING = 0x04,           // Connection keepalive ping
    MSG_PONG = 0x05,           // Ping response
    MSG_ERROR = 0x06           // Error message
} message_type_t;
```

### Protocol Flags

```c
#define FLAG_NONE           0x0000  // No special flags
#define FLAG_COMPRESSED     0x0001  // Payload is compressed (future)
#define FLAG_ENCRYPTED      0x0002  // Payload is encrypted (future)
#define FLAG_FRAGMENTED     0x0004  // Message is fragmented (future)
```

## API Functions

### Core Functions

#### `protocol_create_message()`
```c
int protocol_create_message(char* buffer, size_t buffer_size, message_type_t type,
                           uint32_t client_id, uint16_t flags, const void* payload, uint32_t payload_size);
```
Creates a complete protocol message in the provided buffer.

**Parameters:**
- `buffer`: Output buffer for the message
- `buffer_size`: Size of the output buffer
- `type`: Message type (MSG_DATA, MSG_PING, etc.)
- `client_id`: Client identifier
- `flags`: Protocol flags
- `payload`: Payload data (can be NULL)
- `payload_size`: Size of payload data

**Returns:** Number of bytes written on success, negative error code on failure

#### `protocol_parse_header()`
```c
int protocol_parse_header(const char* buffer, size_t buffer_size, udp_bridge_header_t* header);
```
Parses a protocol header from a buffer and converts from network byte order.

**Parameters:**
- `buffer`: Input buffer containing header
- `buffer_size`: Size of input buffer
- `header`: Output header structure

**Returns:** ERROR_NONE on success, error code on failure

#### `protocol_validate_message()`
```c
int protocol_validate_message(const char* buffer, size_t buffer_size);
```
Validates a complete protocol message including checksum verification.

**Parameters:**
- `buffer`: Buffer containing complete message
- `buffer_size`: Size of the buffer

**Returns:** ERROR_NONE if valid, error code if invalid

#### `protocol_extract_payload()`
```c
int protocol_extract_payload(const char* buffer, size_t buffer_size, 
                            const char** payload, uint32_t* payload_size);
```
Extracts payload data from a protocol message.

**Parameters:**
- `buffer`: Message buffer
- `buffer_size`: Size of message buffer
- `payload`: Output pointer to payload (points into buffer)
- `payload_size`: Output size of payload

**Returns:** ERROR_NONE on success, error code on failure

### Utility Functions

#### `protocol_create_simple_message()`
```c
int protocol_create_simple_message(char* buffer, size_t buffer_size, 
                                  message_type_t type, uint32_t client_id);
```
Creates a message with no payload (shorthand for common case).

#### `protocol_calculate_checksum()`
```c
uint32_t protocol_calculate_checksum(const void* data, size_t size);
```
Calculates CRC32 checksum of data.

#### `protocol_get_message_size()`
```c
size_t protocol_get_message_size(const udp_bridge_header_t* header);
```
Returns total message size (header + payload).

### String Functions

#### `protocol_message_type_string()`
```c
const char* protocol_message_type_string(message_type_t type);
```
Returns string representation of message type for debugging.

#### `protocol_error_string()`
```c
const char* protocol_error_string(uint32_t error_code);
```
Returns string representation of error code for debugging.

## Error Codes

```c
#define ERROR_NONE              0   // No error
#define ERROR_INVALID_HEADER    1   // Invalid header format
#define ERROR_INVALID_CHECKSUM  2   // Checksum validation failed
#define ERROR_CLIENT_NOT_FOUND  3   // Client ID not found
#define ERROR_PAYLOAD_TOO_LARGE 4   // Payload exceeds maximum size
#define ERROR_INTERNAL_ERROR    5   // Internal processing error
```

## Protocol Flow Examples

### Client Registration
```
1. Android → Server: MSG_CLIENT_REGISTER
   - client_id: 0 (new client)
   - payload: client_register_payload_t

2. Server → Android: MSG_CLIENT_REGISTER
   - client_id: [assigned_id]
   - payload: success/error info
```

### UDP Data Transfer
```
1. UDP Client → Android: UDP packet

2. Android → Server: MSG_DATA
   - client_id: [assigned_id]
   - payload: UDP packet data

3. Server → Target: Forward UDP packet

4. Target → Server: UDP response

5. Server → Android: MSG_DATA
   - client_id: [assigned_id]
   - payload: UDP response data

6. Android → UDP Client: Forward UDP response
```

### Connection Keepalive
```
1. Android → Server: MSG_PING
   - client_id: 0
   - payload: empty

2. Server → Android: MSG_PONG
   - client_id: 0
   - payload: empty
```

## Constants

```c
#define UDP_BRIDGE_MAGIC "UDPB"
#define UDP_BRIDGE_VERSION 1
#define UDP_BRIDGE_HEADER_SIZE 20
#define PROTOCOL_MAX_PAYLOAD_SIZE (65536 - UDP_BRIDGE_HEADER_SIZE)
```

## Usage Example

```c
#include "protocol.h"

// Create a data message
char buffer[1024];
const char* udp_data = "Hello, UDP!";
int result = protocol_create_message(buffer, sizeof(buffer), MSG_DATA, 
                                   123, FLAG_NONE, udp_data, strlen(udp_data));

if (result > 0) {
    // Message created successfully
    // Send buffer over TCP connection
    send(tcp_socket, buffer, result, 0);
}

// Parse received message
udp_bridge_header_t header;
if (protocol_parse_header(received_buffer, received_size, &header) == ERROR_NONE) {
    // Extract payload
    const char* payload;
    uint32_t payload_size;
    if (protocol_extract_payload(received_buffer, received_size, &payload, &payload_size) == ERROR_NONE) {
        // Process payload data
        process_udp_data(header.client_id, payload, payload_size);
    }
}
```

## Thread Safety

The protocol functions are stateless and thread-safe for read operations. When using with multiple threads:

- Message creation and parsing functions are safe to call concurrently
- Client ID management should be synchronized externally
- Checksum calculation is thread-safe

## Performance Considerations

- Header size is fixed at 20 bytes
- Maximum payload size is ~65KB
- CRC32 checksum provides good error detection with minimal overhead
- Network byte order conversion is handled automatically

## Version Compatibility

Current version: 1

Future versions will maintain backward compatibility by:
- Checking version field in header
- Graceful handling of unknown message types
- Optional flag-based feature negotiation
