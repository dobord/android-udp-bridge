#ifndef CLIENT_MANAGER_H
#define CLIENT_MANAGER_H

#include "protocol_common.h"
#include <pthread.h>
#include <time.h>

// Android logging for client manager
#ifdef __ANDROID__
#include <android/log.h>
#define CM_LOG_TAG "ClientManager"
#define CM_LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, CM_LOG_TAG, __VA_ARGS__)
#define CM_LOGI(...) __android_log_print(ANDROID_LOG_INFO, CM_LOG_TAG, __VA_ARGS__)
#define CM_LOGW(...) __android_log_print(ANDROID_LOG_WARN, CM_LOG_TAG, __VA_ARGS__)
#define CM_LOGE(...) __android_log_print(ANDROID_LOG_ERROR, CM_LOG_TAG, __VA_ARGS__)
#else
#include <stdio.h>
#define CM_LOG_TAG "ClientManager"
#define CM_LOGD(...) printf("[DEBUG] " CM_LOG_TAG ": " __VA_ARGS__); printf("\n")
#define CM_LOGI(...) printf("[INFO] " CM_LOG_TAG ": " __VA_ARGS__); printf("\n")
#define CM_LOGW(...) printf("[WARN] " CM_LOG_TAG ": " __VA_ARGS__); printf("\n")
#define CM_LOGE(...) printf("[ERROR] " CM_LOG_TAG ": " __VA_ARGS__); printf("\n")
#endif

// Configuration constants
#define CLIENT_TIMEOUT_SECONDS 300      // 5 minutes default timeout
#define MAX_CLIENTS 1000                // Maximum number of clients
#define CLIENT_CLEANUP_INTERVAL 60      // Cleanup every 60 seconds

// Client state flags
#define CLIENT_STATE_ACTIVE     0x01
#define CLIENT_STATE_TIMEOUT    0x02
#define CLIENT_STATE_ERROR      0x04

// Client entry structure
typedef struct client_entry {
    uint32_t client_id;                 // Unique client identifier
    struct sockaddr_in client_addr;     // Client UDP address
    time_t created_time;                // When client was registered
    time_t last_activity;               // Last packet timestamp
    uint32_t state_flags;               // Client state flags
    
    // Statistics
    uint64_t packets_received;          // Total packets from client
    uint64_t packets_sent;              // Total packets to client
    uint64_t bytes_received;            // Total bytes from client
    uint64_t bytes_sent;                // Total bytes to client
    
    // Error tracking
    uint32_t error_count;               // Number of errors
    uint32_t last_error_code;           // Last error that occurred
    
    struct client_entry* next;          // Linked list pointer
} client_entry_t;

// Client manager structure
typedef struct {
    client_entry_t* head;               // Head of client list
    uint32_t client_count;              // Current number of clients
    uint32_t next_client_id;            // Next ID to assign
    time_t last_cleanup;                // Last cleanup timestamp
    
    // Configuration
    time_t client_timeout;              // Client timeout in seconds
    uint32_t max_clients;               // Maximum allowed clients
    
    // Statistics
    uint64_t total_clients_created;     // Total clients ever created
    uint64_t total_clients_expired;     // Total clients that expired
    uint64_t total_packets_processed;   // Total packets processed
    
    pthread_mutex_t mutex;              // Thread safety
} client_manager_t;

// Client manager functions
/**
 * Create and initialize a new client manager
 * @param timeout Client timeout in seconds (0 for default)
 * @param max_clients Maximum clients (0 for default)
 * @return Pointer to client manager or NULL on error
 */
client_manager_t* client_manager_create(time_t timeout, uint32_t max_clients);

/**
 * Destroy client manager and free all resources
 * @param manager Pointer to client manager
 */
void client_manager_destroy(client_manager_t* manager);

/**
 * Add a new client or update existing one
 * @param manager Pointer to client manager
 * @param addr Client address
 * @return Client ID or 0 on error
 */
uint32_t client_manager_add_client(client_manager_t* manager, const struct sockaddr_in* addr);

/**
 * Find client by ID
 * @param manager Pointer to client manager
 * @param client_id Client ID to search for
 * @return Pointer to client entry or NULL if not found
 */
client_entry_t* client_manager_find_by_id(client_manager_t* manager, uint32_t client_id);

/**
 * Find client by address
 * @param manager Pointer to client manager
 * @param addr Client address to search for
 * @return Pointer to client entry or NULL if not found
 */
client_entry_t* client_manager_find_by_addr(client_manager_t* manager, const struct sockaddr_in* addr);

/**
 * Update client activity timestamp
 * @param manager Pointer to client manager
 * @param client_id Client ID
 * @return 0 on success, -1 on error
 */
int client_manager_update_activity(client_manager_t* manager, uint32_t client_id);

/**
 * Update client statistics
 * @param manager Pointer to client manager
 * @param client_id Client ID
 * @param bytes_received Bytes received from client
 * @param bytes_sent Bytes sent to client
 * @return 0 on success, -1 on error
 */
int client_manager_update_stats(client_manager_t* manager, uint32_t client_id, 
                               uint32_t bytes_received, uint32_t bytes_sent);

/**
 * Set client error state
 * @param manager Pointer to client manager
 * @param client_id Client ID
 * @param error_code Error code
 * @return 0 on success, -1 on error
 */
int client_manager_set_error(client_manager_t* manager, uint32_t client_id, uint32_t error_code);

/**
 * Remove expired clients
 * @param manager Pointer to client manager
 * @return Number of clients removed
 */
int client_manager_cleanup_expired(client_manager_t* manager);

/**
 * Remove specific client
 * @param manager Pointer to client manager
 * @param client_id Client ID to remove
 * @return 0 on success, -1 on error
 */
int client_manager_remove_client(client_manager_t* manager, uint32_t client_id);

/**
 * Get client count
 * @param manager Pointer to client manager
 * @return Current number of clients
 */
uint32_t client_manager_get_count(client_manager_t* manager);

/**
 * Get client statistics
 * @param manager Pointer to client manager
 * @param client_id Client ID
 * @param packets_rx Pointer to store received packet count
 * @param packets_tx Pointer to store sent packet count
 * @param bytes_rx Pointer to store received byte count
 * @param bytes_tx Pointer to store sent byte count
 * @return 0 on success, -1 on error
 */
int client_manager_get_stats(client_manager_t* manager, uint32_t client_id,
                            uint64_t* packets_rx, uint64_t* packets_tx,
                            uint64_t* bytes_rx, uint64_t* bytes_tx);

/**
 * Print client manager statistics (for debugging)
 * @param manager Pointer to client manager
 */
void client_manager_print_stats(client_manager_t* manager);

/**
 * Check if automatic cleanup is needed and perform it
 * @param manager Pointer to client manager
 * @return Number of clients removed or -1 on error
 */
int client_manager_auto_cleanup(client_manager_t* manager);

#endif // CLIENT_MANAGER_H
