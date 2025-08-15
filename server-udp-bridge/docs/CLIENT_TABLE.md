# Client Table Module

Client management module for the UDP Bridge Server. Provides efficient handling of multiple client connections with automatic lifecycle management and statistics.

## Features

- **Client management**: Add, find, remove clients with unique IDs
- **Automatic timeouts**: Background cleanup of inactive clients
- **Statistics**: Track bytes, packets, activity time
- **Thread safety**: Mutex-based safe access from multiple threads
- **Scalability**: Supports thousands of concurrent clients

## API

### Core functions

```c
// Create client table
client_table_t* client_table_create(uint32_t max_clients, time_t timeout_seconds);

// Destroy table
void client_table_destroy(client_table_t* table);

// Add client
uint32_t client_table_add(client_table_t* table, int tcp_socket);

// Find client
client_entry_t* client_table_find(client_table_t* table, uint32_t client_id);

// Remove client
int client_table_remove(client_table_t* table, uint32_t client_id);
```

### Activity management

```c
// Update activity timestamp
void client_table_update_activity(client_table_t* table, uint32_t client_id);

// Update statistics
void client_table_update_stats(client_table_t* table, uint32_t client_id, 
                              uint64_t bytes_received, uint64_t bytes_sent);
```

### Automatic cleanup

```c
// Start background cleanup thread
int client_table_start_cleanup(client_table_t* table);

// Stop background thread
void client_table_stop_cleanup(client_table_t* table);

// Manual cleanup of expired clients
int client_table_cleanup_expired(client_table_t* table);
```

### Monitoring

```c
// Get active client count
uint32_t client_table_get_count(client_table_t* table);

// Print statistics (debug)
void client_table_print_stats(client_table_t* table);
```

## Data structures

### client_entry_t
```c
typedef struct client_entry {
    uint32_t client_id;                    // Unique client identifier
    time_t last_activity;                  // Last activity time
    uint64_t bytes_received;               // Total bytes received from client
    uint64_t bytes_sent;                   // Total bytes sent to client
    uint32_t packet_count;                 // Total packet count
    int tcp_socket;                        // TCP socket for communication with Android
    struct client_entry* next;             // Next element in list
} client_entry_t;
```

### client_table_t
```c
typedef struct {
    client_entry_t* head;                  // Head of linked list
    uint32_t count;                        // Number of active clients
    uint32_t next_id;                      // Next available ID
    uint32_t max_clients;                  // Maximum number of clients
    time_t timeout_seconds;                // Client timeout in seconds
    pthread_mutex_t mutex;                 // Mutex for thread safety
    pthread_t cleanup_thread;              // Background cleanup thread
    int cleanup_running;                   // Cleanup thread running flag
} client_table_t;
```

## Usage examples

### Basic usage

```c
#include "client_table.h"

// Create table for 1000 clients with 300s timeout
client_table_t* table = client_table_create(1000, 300);
if (!table) {
    fprintf(stderr, "Failed to create client table\\n");
    return -1;
}

// Add client
int client_socket = accept(server_socket, ...);
uint32_t client_id = client_table_add(table, client_socket);
if (client_id == 0) {
    fprintf(stderr, "Failed to add client\\n");
    close(client_socket);
} else {
    printf("Added client with ID %u\\n", client_id);
}

// Update activity on data receipt
client_table_update_activity(table, client_id);
client_table_update_stats(table, client_id, received_bytes, sent_bytes);

// Cleanup
client_table_destroy(table);
```

### Using automatic cleanup

```c
// Create table
client_table_t* table = client_table_create(1000, 300);

// Start background cleanup thread
if (client_table_start_cleanup(table) != 0) {
    fprintf(stderr, "Failed to start cleanup thread\\n");
    client_table_destroy(table);
    return -1;
}

// Use table...
// Background thread removes inactive clients automatically

// Cleanup (automatically stops cleanup thread)
client_table_destroy(table);
```

### Monitoring statistics

```c
// Print current statistics
client_table_print_stats(table);

// Get active client count
uint32_t active_clients = client_table_get_count(table);
printf("Active clients: %u\\n", active_clients);

// Find specific client
client_entry_t* client = client_table_find(table, client_id);
if (client) {
    printf("Client %u: RX=%lu, TX=%lu, packets=%u\\n",
           client->client_id, client->bytes_received, 
           client->bytes_sent, client->packet_count);
}
```

## Build and test

### Build

```bash
# Build all components
make all

# Build only client_table tests
make client-table-test

# Build demo
make demo
```

### Testing

```bash
# Run all tests
make test

# Run only client_table tests
make client-table-test

# Run integration demo
make demo
```

## Performance

### Characteristics

- **Lookup time**: O(n) worst case, O(1) average for small client counts
- **Memory**: ~64 bytes per client + system overhead
- **Scalability**: Tested with 1000+ concurrent clients
- **Thread safety**: Full with pthread_mutex

### Optimizations

- Linked list for fast add/remove
- Background cleanup thread to minimize locking
- Efficient memory management
- Minimal system calls

## Integration

Module designed to integrate with:

- **UDP Forwarder**: Track clients for packet routing
- **Protocol Handler**: Validate client_id in protocol messages  
- **Main Server**: Manage TCP connections
- **Logging System**: Statistics and monitoring

## Security

- Validate all input parameters
- Protect against buffer overflows
- Safe memory management
- Graceful handling of network errors

## Diagnostics

### Logs

Module emits detailed logs:
- Client add/remove
- Cleanup statistics
- Errors and warnings

### Debugging

Function `client_table_print_stats()` provides:
- Number of active clients
- Per-client statistics
- Inactivity timing info
- Background thread state

## Compatibility

- **Platforms**: Linux, macOS (with pthread)
- **Compilers**: GCC, Clang
- **Standard**: C99
- **Dependencies**: pthread, C standard library
