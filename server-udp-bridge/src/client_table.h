#ifndef CLIENT_TABLE_H
#define CLIENT_TABLE_H

#include <stdint.h>
#include <time.h>
#include <pthread.h>

// Client entry structure
typedef struct client_entry {
    uint32_t client_id;                    // Unique client identifier
    time_t last_activity;                  // Last activity timestamp
    uint64_t bytes_received;               // Total bytes received from this client
    uint64_t bytes_sent;                   // Total bytes sent to this client
    uint32_t packet_count;                 // Total packet count
    int tcp_socket;                        // TCP socket for communication back to Android
    struct client_entry* next;             // Next entry in linked list
} client_entry_t;

// Client table structure
typedef struct {
    client_entry_t* head;                  // Head of linked list
    uint32_t count;                        // Number of active clients
    uint32_t next_id;                      // Next available client ID
    uint32_t max_clients;                  // Maximum allowed clients
    time_t timeout_seconds;                // Client timeout in seconds
    pthread_mutex_t mutex;                 // Thread safety mutex
    pthread_t cleanup_thread;              // Background cleanup thread
    int cleanup_running;                   // Cleanup thread running flag
} client_table_t;

// Function prototypes

/**
 * Create a new client table
 * @param max_clients Maximum number of clients allowed
 * @param timeout_seconds Client timeout in seconds
 * @return Pointer to new client table or NULL on error
 */
client_table_t* client_table_create(uint32_t max_clients, time_t timeout_seconds);

/**
 * Destroy client table and free all resources
 * @param table Client table to destroy
 */
void client_table_destroy(client_table_t* table);

/**
 * Add a new client to the table
 * @param table Client table
 * @param tcp_socket TCP socket for communication
 * @return New client ID or 0 on error
 */
uint32_t client_table_add(client_table_t* table, int tcp_socket);

/**
 * Find a client by ID
 * @param table Client table
 * @param client_id Client ID to find
 * @return Pointer to client entry or NULL if not found
 */
client_entry_t* client_table_find(client_table_t* table, uint32_t client_id);

/**
 * Find client by TCP socket
 * @param table Client table
 * @param tcp_socket TCP socket to search for
 * @return Pointer to client entry or NULL if not found
 */
client_entry_t* client_table_find_by_socket(client_table_t* table, int tcp_socket);

/**
 * Remove a client by ID
 * @param table Client table
 * @param client_id Client ID to remove
 * @return 0 on success, -1 on error
 */
int client_table_remove(client_table_t* table, uint32_t client_id);

/**
 * Update client activity timestamp
 * @param table Client table
 * @param client_id Client ID
 */
void client_table_update_activity(client_table_t* table, uint32_t client_id);

/**
 * Update client statistics
 * @param table Client table
 * @param client_id Client ID
 * @param bytes_received Bytes received from client
 * @param bytes_sent Bytes sent to client
 */
void client_table_update_stats(client_table_t* table, uint32_t client_id, 
                              uint64_t bytes_received, uint64_t bytes_sent);

/**
 * Remove expired clients
 * @param table Client table
 * @return Number of clients removed
 */
int client_table_cleanup_expired(client_table_t* table);

/**
 * Get number of active clients
 * @param table Client table
 * @return Number of active clients
 */
uint32_t client_table_get_count(client_table_t* table);

/**
 * Print client table statistics (for debugging)
 * @param table Client table
 */
void client_table_print_stats(client_table_t* table);

/**
 * Start background cleanup thread
 * @param table Client table
 * @return 0 on success, -1 on error
 */
int client_table_start_cleanup(client_table_t* table);

/**
 * Stop background cleanup thread
 * @param table Client table
 */
void client_table_stop_cleanup(client_table_t* table);

#endif // CLIENT_TABLE_H
