#include "udp_bridge_protocol.h"
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <pthread.h>

// CRC32 table for checksum calculation
static const uint32_t crc32_table[256] = {
    0x00000000, 0x77073096, 0xEE0E612C, 0x990951BA, 0x076DC419, 0x706AF48F,
    0xE963A535, 0x9E6495A3, 0x0EDB8832, 0x79DCB8A4, 0xE0D5E91E, 0x97D2D988,
    0x09B64C2B, 0x7EB17CBD, 0xE7B82D07, 0x90BF1D91, 0x1DB71064, 0x6AB020F2,
    0xF3B97148, 0x84BE41DE, 0x1ADAD47D, 0x6DDDE4EB, 0xF4D4B551, 0x83D385C7,
    0x136C9856, 0x646BA8C0, 0xFD62F97A, 0x8A65C9EC, 0x14015C4F, 0x63066CD9,
    0xFA0F3D63, 0x8D080DF5, 0x3B6E20C8, 0x4C69105E, 0xD56041E4, 0xA2677172,
    0x3C03E4D1, 0x4B04D447, 0xD20D85FD, 0xA50AB56B, 0x35B5A8FA, 0x42B2986C,
    0xDBBBC9D6, 0xACBCF940, 0x32D86CE3, 0x45DF5C75, 0xDCD60DCF, 0xABD13D59,
    0x26D930AC, 0x51DE003A, 0xC8D75180, 0xBFD06116, 0x21B4F4B5, 0x56B3C423,
    0xCFBA9599, 0xB8BDA50F, 0x2802B89E, 0x5F058808, 0xC60CD9B2, 0xB10BE924,
    0x2F6F7C87, 0x58684C11, 0xC1611DAB, 0xB6662D3D, 0x76DC4190, 0x01DB7106,
    0x98D220BC, 0xEFD5102A, 0x71B18589, 0x06B6B51F, 0x9FBFE4A5, 0xE8B8D433,
    0x7807C9A2, 0x0F00F934, 0x9609A88E, 0xE10E9818, 0x7F6A0DBB, 0x086D3D2D,
    0x91646C97, 0xE6635C01, 0x6B6B51F4, 0x1C6C6162, 0x856530D8, 0xF262004E,
    0x6C0695ED, 0x1B01A57B, 0x8208F4C1, 0xF50FC457, 0x65B0D9C6, 0x12B7E950,
    0x8BBEB8EA, 0xFCB9887C, 0x62DD1DDF, 0x15DA2D49, 0x8CD37CF3, 0xFBD44C65,
    0x4DB26158, 0x3AB551CE, 0xA3BC0074, 0xD4BB30E2, 0x4ADFA541, 0x3DD895D7,
    0xA4D1C46D, 0xD3D6F4FB, 0x4369E96A, 0x346ED9FC, 0xAD678846, 0xDA60B8D0,
    0x44042D73, 0x33031DE5, 0xAA0A4C5F, 0xDD0D7CC9, 0x5005713C, 0x270241AA,
    0xBE0B1010, 0xC90C2086, 0x5768B525, 0x206F85B3, 0xB966D409, 0xCE61E49F,
    0x5EDEF90E, 0x29D9C998, 0xB0D09822, 0xC7D7A8B4, 0x59B33D17, 0x2EB40D81,
    0xB7BD5C3B, 0xC0BA6CAD, 0xEDB88320, 0x9ABFB3B6, 0x03B6E20C, 0x74B1D29A,
    0xEAD54739, 0x9DD277AF, 0x04DB2615, 0x73DC1683, 0xE3630B12, 0x94643B84,
    0x0D6D6A3E, 0x7A6A5AA8, 0xE40ECF0B, 0x9309FF9D, 0x0A00AE27, 0x7D079EB1,
    0xF00F9344, 0x8708A3D2, 0x1E01F268, 0x6906C2FE, 0xF762575D, 0x806567CB,
    0x196C3671, 0x6E6B06E7, 0xFED41B76, 0x89D32BE0, 0x10DA7A5A, 0x67DD4ACC,
    0xF9B9DF6F, 0x8EBEEFF9, 0x17B7BE43, 0x60B08ED5, 0xD6D6A3E8, 0xA1D1937E,
    0x38D8C2C4, 0x4FDFF252, 0xD1BB67F1, 0xA6BC5767, 0x3FB506DD, 0x48B2364B,
    0xD80D2BDA, 0xAF0A1B4C, 0x36034AF6, 0x41047A60, 0xDF60EFC3, 0xA867DF55,
    0x316E8EEF, 0x4669BE79, 0xCB61B38C, 0xBC66831A, 0x256FD2A0, 0x5268E236,
    0xCC0C7795, 0xBB0B4703, 0x220216B9, 0x5505262F, 0xC5BA3BBE, 0xB2BD0B28,
    0x2BB45A92, 0x5CB36A04, 0xC2D7FFA7, 0xB5D0CF31, 0x2CD99E8B, 0x5BDEAE1D,
    0x9B64C2B0, 0xEC63F226, 0x756AA39C, 0x026D930A, 0x9C0906A9, 0xEB0E363F,
    0x72076785, 0x05005713, 0x95BF4A82, 0xE2B87A14, 0x7BB12BAE, 0x0CB61B38,
    0x92D28E9B, 0xE5D5BE0D, 0x7CDCEFB7, 0x0BDBDF21, 0x86D3D2D4, 0xF1D4E242,
    0x68DDB3F8, 0x1FDA836E, 0x81BE16CD, 0xF6B9265B, 0x6FB077E1, 0x18B74777,
    0x88085AE6, 0xFF0F6A70, 0x66063BCA, 0x11010B5C, 0x8F659EFF, 0xF862AE69,
    0x616BFFD3, 0x166CCF45, 0xA00AE278, 0xD70DD2EE, 0x4E048354, 0x3903B3C2,
    0xA7672661, 0xD06016F7, 0x4969474D, 0x3E6E77DB, 0xAED16A4A, 0xD9D65ADC,
    0x40DF0B66, 0x37D83BF0, 0xA9BCAE53, 0xDEBB9EC5, 0x47B2CF7F, 0x30B5FFE9,
    0xBDBDF21C, 0xCABAC28A, 0x53B39330, 0x24B4A3A6, 0xBAD03605, 0xCDD70693,
    0x54DE5729, 0x23D967BF, 0xB3667A2E, 0xC4614AB8, 0x5D681B02, 0x2A6F2B94,
    0xB40BBE37, 0xC30C8EA1, 0x5A05DF1B, 0x2D02EF8D
};

// Global protocol context
static android_protocol_ctx_t* g_protocol_ctx = NULL;

// Internal helper functions
static uint32_t calculate_crc32(const void* data, size_t size) {
    const uint8_t* bytes = (const uint8_t*)data;
    uint32_t crc = 0xFFFFFFFF;
    
    for (size_t i = 0; i < size; i++) {
        crc = crc32_table[(crc ^ bytes[i]) & 0xFF] ^ (crc >> 8);
    }
    
    return crc ^ 0xFFFFFFFF;
}

// Protocol implementation
uint32_t protocol_calculate_checksum(const void* data, size_t size) {
    return calculate_crc32(data, size);
}

int protocol_parse_header(const char* buffer, size_t buffer_size, udp_bridge_header_t* header) {
    if (!buffer || !header || buffer_size < UDP_BRIDGE_HEADER_SIZE) {
        return ERROR_INVALID_HEADER;
    }
    
    memcpy(header, buffer, UDP_BRIDGE_HEADER_SIZE);
    
    // Convert network byte order to host byte order
    header->flags = ntohs(header->flags);
    header->client_id = ntohl(header->client_id);
    header->payload_size = ntohl(header->payload_size);
    header->checksum = ntohl(header->checksum);
    
    return ERROR_NONE;
}

int protocol_validate_header(const udp_bridge_header_t* header) {
    if (!header) {
        return ERROR_INVALID_HEADER;
    }
    
    // Check magic bytes
    if (memcmp(header->magic, UDP_BRIDGE_MAGIC, 4) != 0) {
        LOGE("Invalid magic bytes");
        return ERROR_INVALID_HEADER;
    }
    
    // Check version
    if (header->version != UDP_BRIDGE_VERSION) {
        LOGE("Invalid protocol version: %d", header->version);
        return ERROR_INVALID_HEADER;
    }
    
    // Check message type
    if (header->message_type < MSG_DATA || header->message_type > MSG_ERROR) {
        LOGE("Invalid message type: %d", header->message_type);
        return ERROR_INVALID_HEADER;
    }
    
    // Check payload size
    if (header->payload_size > PROTOCOL_MAX_PAYLOAD_SIZE) {
        LOGE("Payload too large: %d", header->payload_size);
        return ERROR_PAYLOAD_TOO_LARGE;
    }
    
    return ERROR_NONE;
}

int protocol_create_message(char* buffer, size_t buffer_size, message_type_t type, 
                           uint32_t client_id, uint16_t flags, const void* payload, uint32_t payload_size) {
    if (!buffer || buffer_size < UDP_BRIDGE_HEADER_SIZE + payload_size) {
        return -ERROR_INVALID_HEADER;
    }
    
    udp_bridge_header_t header;
    memcpy(header.magic, UDP_BRIDGE_MAGIC, 4);
    header.version = UDP_BRIDGE_VERSION;
    header.message_type = type;
    header.flags = htons(flags);
    header.client_id = htonl(client_id);
    header.payload_size = htonl(payload_size);
    
    // Copy header to buffer
    memcpy(buffer, &header, UDP_BRIDGE_HEADER_SIZE);
    
    // Copy payload if provided
    if (payload && payload_size > 0) {
        memcpy(buffer + UDP_BRIDGE_HEADER_SIZE, payload, payload_size);
    }
    
    // Calculate and set checksum
    uint32_t checksum = protocol_calculate_checksum(buffer, UDP_BRIDGE_HEADER_SIZE + payload_size);
    uint32_t checksum_network = htonl(checksum);
    memcpy(buffer + offsetof(udp_bridge_header_t, checksum), &checksum_network, sizeof(uint32_t));
    
    return UDP_BRIDGE_HEADER_SIZE + payload_size;
}

const char* protocol_message_type_string(message_type_t type) {
    switch (type) {
        case MSG_DATA: return "DATA";
        case MSG_CLIENT_REGISTER: return "CLIENT_REGISTER";
        case MSG_CLIENT_TIMEOUT: return "CLIENT_TIMEOUT";
        case MSG_PING: return "PING";
        case MSG_PONG: return "PONG";
        case MSG_ERROR: return "ERROR";
        default: return "UNKNOWN";
    }
}

const char* protocol_error_string(uint32_t error_code) {
    switch (error_code) {
        case ERROR_NONE: return "No error";
        case ERROR_INVALID_HEADER: return "Invalid header";
        case ERROR_INVALID_CHECKSUM: return "Invalid checksum";
        case ERROR_CLIENT_NOT_FOUND: return "Client not found";
        case ERROR_PAYLOAD_TOO_LARGE: return "Payload too large";
        case ERROR_INTERNAL_ERROR: return "Internal error";
        default: return "Unknown error";
    }
}

// Android specific implementation
int android_protocol_init(android_protocol_ctx_t* ctx, int local_port) {
    if (!ctx) {
        return -1;
    }
    
    memset(ctx, 0, sizeof(android_protocol_ctx_t));
    ctx->next_client_id = 1;
    ctx->tcp_socket = -1;
    ctx->udp_socket = -1;
    ctx->local_port = local_port;
    
    if (pthread_mutex_init(&ctx->mutex, NULL) != 0) {
        LOGE("Failed to initialize mutex");
        return -1;
    }
    
    // Create UDP socket for local communication
    ctx->udp_socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (ctx->udp_socket < 0) {
        LOGE("Failed to create UDP socket: %s", strerror(errno));
        pthread_mutex_destroy(&ctx->mutex);
        return -1;
    }
    
    // Bind to local port
    struct sockaddr_in local_addr;
    memset(&local_addr, 0, sizeof(local_addr));
    local_addr.sin_family = AF_INET;
    local_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    local_addr.sin_port = htons(local_port);
    
    if (bind(ctx->udp_socket, (struct sockaddr*)&local_addr, sizeof(local_addr)) < 0) {
        LOGE("Failed to bind UDP socket to port %d: %s", local_port, strerror(errno));
        close(ctx->udp_socket);
        pthread_mutex_destroy(&ctx->mutex);
        return -1;
    }
    
    LOGI("Protocol initialized on UDP port %d", local_port);
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
    
    // Free client list
    android_client_t* current = ctx->clients;
    while (current) {
        android_client_t* next = current->next;
        free(current);
        current = next;
    }
    ctx->clients = NULL;
    
    pthread_mutex_unlock(&ctx->mutex);
    pthread_mutex_destroy(&ctx->mutex);
    
    LOGI("Protocol cleanup completed");
}

android_client_t* android_find_or_create_client(android_protocol_ctx_t* ctx, struct sockaddr_in* addr) {
    if (!ctx || !addr) {
        return NULL;
    }
    
    pthread_mutex_lock(&ctx->mutex);
    
    // Search for existing client
    android_client_t* current = ctx->clients;
    while (current) {
        if (current->addr.sin_addr.s_addr == addr->sin_addr.s_addr &&
            current->addr.sin_port == addr->sin_port) {
            current->last_activity = time(NULL);
            pthread_mutex_unlock(&ctx->mutex);
            return current;
        }
        current = current->next;
    }
    
    // Create new client
    android_client_t* new_client = malloc(sizeof(android_client_t));
    if (!new_client) {
        LOGE("Failed to allocate memory for new client");
        pthread_mutex_unlock(&ctx->mutex);
        return NULL;
    }
    
    new_client->client_id = ctx->next_client_id++;
    new_client->addr = *addr;
    new_client->last_activity = time(NULL);
    new_client->packet_count = 0;
    new_client->next = ctx->clients;
    ctx->clients = new_client;
    
    LOGI("Created new client with ID %d", new_client->client_id);
    
    pthread_mutex_unlock(&ctx->mutex);
    return new_client;
}

android_client_t* android_find_client_by_id(android_protocol_ctx_t* ctx, uint32_t client_id) {
    if (!ctx) {
        return NULL;
    }
    
    pthread_mutex_lock(&ctx->mutex);
    
    android_client_t* current = ctx->clients;
    while (current) {
        if (current->client_id == client_id) {
            pthread_mutex_unlock(&ctx->mutex);
            return current;
        }
        current = current->next;
    }
    
    pthread_mutex_unlock(&ctx->mutex);
    return NULL;
}

int android_send_protocol_message(android_protocol_ctx_t* ctx, message_type_t type, 
                                 uint32_t client_id, const void* data, size_t size) {
    if (!ctx || ctx->tcp_socket < 0) {
        return -1;
    }
    
    char buffer[PROTOCOL_MAX_PAYLOAD_SIZE + UDP_BRIDGE_HEADER_SIZE];
    int message_size = protocol_create_message(buffer, sizeof(buffer), type, client_id, FLAG_NONE, data, size);
    
    if (message_size < 0) {
        LOGE("Failed to create protocol message");
        return -1;
    }
    
    ssize_t sent = send(ctx->tcp_socket, buffer, message_size, 0);
    if (sent != message_size) {
        LOGE("Failed to send protocol message: %s", strerror(errno));
        return -1;
    }
    
    LOGD("Sent %s message (client_id=%d, size=%d)", protocol_message_type_string(type), client_id, message_size);
    return 0;
}

int android_bridge_connect(android_protocol_ctx_t* ctx, const char* server_host, int server_port) {
    if (!ctx || !server_host) {
        return -1;
    }
    
    if (ctx->tcp_socket >= 0) {
        close(ctx->tcp_socket);
    }
    
    ctx->tcp_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (ctx->tcp_socket < 0) {
        LOGE("Failed to create TCP socket: %s", strerror(errno));
        return -1;
    }
    
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
    
    if (connect(ctx->tcp_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        LOGE("Failed to connect to bridge server %s:%d: %s", server_host, server_port, strerror(errno));
        close(ctx->tcp_socket);
        ctx->tcp_socket = -1;
        return -1;
    }
    
    LOGI("Connected to bridge server %s:%d", server_host, server_port);
    return 0;
}

void android_bridge_disconnect(android_protocol_ctx_t* ctx) {
    if (!ctx) {
        return;
    }
    
    if (ctx->tcp_socket >= 0) {
        close(ctx->tcp_socket);
        ctx->tcp_socket = -1;
        LOGI("Disconnected from bridge server");
    }
}

int android_bridge_is_connected(android_protocol_ctx_t* ctx) {
    return (ctx && ctx->tcp_socket >= 0) ? 1 : 0;
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
