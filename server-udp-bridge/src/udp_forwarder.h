#ifndef UDP_FORWARDER_H
#define UDP_FORWARDER_H

#include <stdint.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include "client_table.h"
#include "protocol.h"

// UDP forwarder configuration
#define UDP_BUFFER_SIZE 65536
#define MAX_RETRIES 3
#define RETRY_DELAY_MS 100

// UDP forwarder structure
typedef struct {
    int udp_socket;                        // UDP socket for target server communication
    struct sockaddr_in target_addr;        // Target UDP server address
    client_table_t* clients;               // Reference to client table
    pthread_t receiver_thread;             // Thread for receiving UDP responses
    pthread_mutex_t send_mutex;            // Mutex for thread-safe sending
    int running;                           // Running flag for threads
    uint64_t total_packets_sent;           // Statistics: total packets sent
    uint64_t total_packets_received;       // Statistics: total packets received
    uint64_t total_bytes_sent;             // Statistics: total bytes sent
    uint64_t total_bytes_received;         // Statistics: total bytes received
    time_t start_time;                     // Forwarder start time
} udp_forwarder_t;

// Function prototypes

/**
 * Create a new UDP forwarder
 * @param target_host Target UDP server hostname/IP
 * @param target_port Target UDP server port
 * @param clients Client table reference
 * @return Pointer to new UDP forwarder or NULL on error
 */
udp_forwarder_t* udp_forwarder_create(const char* target_host, 
                                     int target_port, 
                                     client_table_t* clients);

/**
 * Destroy UDP forwarder and free all resources
 * @param forwarder UDP forwarder to destroy
 */
void udp_forwarder_destroy(udp_forwarder_t* forwarder);

/**
 * Start UDP forwarder threads
 * @param forwarder UDP forwarder instance
 * @return 0 on success, -1 on error
 */
int udp_forwarder_start(udp_forwarder_t* forwarder);

/**
 * Stop UDP forwarder threads
 * @param forwarder UDP forwarder instance
 */
void udp_forwarder_stop(udp_forwarder_t* forwarder);

/**
 * Send UDP data to target server on behalf of a client
 * @param forwarder UDP forwarder instance
 * @param client_id Client identifier
 * @param data Data to send
 * @param size Size of data
 * @return Number of bytes sent or -1 on error
 */
int udp_forwarder_send(udp_forwarder_t* forwarder, 
                      uint32_t client_id,
                      const void* data, 
                      size_t size);

/**
 * Thread function for receiving UDP responses from target server
 * @param arg UDP forwarder instance (udp_forwarder_t*)
 * @return NULL
 */
void* udp_forwarder_receiver_thread(void* arg);

/**
 * Send response back to Android client through TCP connection
 * @param forwarder UDP forwarder instance
 * @param client_id Client identifier
 * @param data Response data
 * @param size Size of response data
 * @return 0 on success, -1 on error
 */
int udp_forwarder_send_response(udp_forwarder_t* forwarder,
                               uint32_t client_id,
                               const void* data,
                               size_t size);

/**
 * Get UDP forwarder statistics
 * @param forwarder UDP forwarder instance
 * @param packets_sent Output: total packets sent
 * @param packets_received Output: total packets received
 * @param bytes_sent Output: total bytes sent
 * @param bytes_received Output: total bytes received
 * @param uptime_seconds Output: uptime in seconds
 */
void udp_forwarder_get_stats(udp_forwarder_t* forwarder,
                            uint64_t* packets_sent,
                            uint64_t* packets_received,
                            uint64_t* bytes_sent,
                            uint64_t* bytes_received,
                            time_t* uptime_seconds);

/**
 * Print UDP forwarder statistics to stdout
 * @param forwarder UDP forwarder instance
 */
void udp_forwarder_print_stats(udp_forwarder_t* forwarder);

#endif // UDP_FORWARDER_H
