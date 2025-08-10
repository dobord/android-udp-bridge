#ifndef UDP_BRIDGE_PROTOCOL_H
#define UDP_BRIDGE_PROTOCOL_H

#include <stdint.h>
#include <stddef.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <time.h>

// Protocol constants
#define UDP_BRIDGE_MAGIC "UDPB"
#define UDP_BRIDGE_VERSION 1
#define UDP_BRIDGE_HEADER_SIZE 20

// Message types
typedef enum {
    MSG_DATA = 0x01,
    MSG_CLIENT_REGISTER = 0x02,
    MSG_CLIENT_TIMEOUT = 0x03,
    MSG_PING = 0x04,
    MSG_PONG = 0x05,
    MSG_ERROR = 0x06
} message_type_t;

// Protocol flags
#define FLAG_NONE           0x0000
#define FLAG_COMPRESSED     0x0001
#define FLAG_ENCRYPTED      0x0002
#define FLAG_FRAGMENTED     0x0004

// Protocol header
typedef struct {
    char magic[4];          // "UDPB" magic bytes
    uint8_t version;        // Protocol version (1)
    uint8_t message_type;   // message_type_t
    uint16_t flags;         // Protocol flags
    uint32_t client_id;     // Client identifier
    uint32_t payload_size;  // Size of payload data
    uint32_t checksum;      // CRC32 of header + payload
} __attribute__((packed)) udp_bridge_header_t;

// Client registration payload
typedef struct {
    struct sockaddr_in client_addr;
    uint32_t capabilities;
    char client_info[64];
} __attribute__((packed)) client_register_payload_t;

// Error payload
typedef struct {
    uint32_t error_code;
    char error_message[128];
} __attribute__((packed)) error_payload_t;

// Error codes
#define ERROR_NONE              0
#define ERROR_INVALID_HEADER    1
#define ERROR_INVALID_CHECKSUM  2
#define ERROR_CLIENT_NOT_FOUND  3
#define ERROR_PAYLOAD_TOO_LARGE 4
#define ERROR_INTERNAL_ERROR    5

// Function prototypes
int protocol_parse_header(const char* buffer, size_t buffer_size, udp_bridge_header_t* header);
int protocol_validate_header(const udp_bridge_header_t* header);
int protocol_create_message(char* buffer, size_t buffer_size, message_type_t type, 
                           uint32_t client_id, uint16_t flags, const void* payload, uint32_t payload_size);
uint32_t protocol_calculate_checksum(const void* data, size_t size);
const char* protocol_message_type_string(message_type_t type);
const char* protocol_error_string(uint32_t error_code);

// Helper macros
#define PROTOCOL_MESSAGE_SIZE(payload_size) (UDP_BRIDGE_HEADER_SIZE + (payload_size))
#define PROTOCOL_MAX_PAYLOAD_SIZE (65536 - UDP_BRIDGE_HEADER_SIZE)

#endif // UDP_BRIDGE_PROTOCOL_H
