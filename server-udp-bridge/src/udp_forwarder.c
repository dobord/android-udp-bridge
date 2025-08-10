// Feature test macros for inet_aton and usleep
#define _GNU_SOURCE
#define _DEFAULT_SOURCE

#include "udp_forwarder.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <sys/time.h>

// Helper function to resolve hostname
static int resolve_hostname(const char* hostname, struct sockaddr_in* addr) {
    struct hostent* he = gethostbyname(hostname);
    if (he == NULL) {
        fprintf(stderr, "Failed to resolve hostname: %s\n", hostname);
        return -1;
    }
    
    memcpy(&addr->sin_addr, he->h_addr_list[0], he->h_length);
    return 0;
}

udp_forwarder_t* udp_forwarder_create(const char* target_host, 
                                     int target_port, 
                                     client_table_t* clients) {
    if (!target_host || target_port <= 0 || !clients) {
        fprintf(stderr, "Invalid parameters for UDP forwarder creation\n");
        return NULL;
    }

    udp_forwarder_t* forwarder = malloc(sizeof(udp_forwarder_t));
    if (!forwarder) {
        fprintf(stderr, "Failed to allocate memory for UDP forwarder\n");
        return NULL;
    }

    // Initialize structure
    memset(forwarder, 0, sizeof(udp_forwarder_t));
    forwarder->clients = clients;
    forwarder->running = 0;
    forwarder->start_time = time(NULL);

    // Create UDP socket
    forwarder->udp_socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (forwarder->udp_socket < 0) {
        fprintf(stderr, "Failed to create UDP socket: %s\n", strerror(errno));
        free(forwarder);
        return NULL;
    }

    // Set socket options for reuse
    int reuse = 1;
    if (setsockopt(forwarder->udp_socket, SOL_SOCKET, SO_REUSEADDR, 
                   &reuse, sizeof(reuse)) < 0) {
        fprintf(stderr, "Failed to set socket options: %s\n", strerror(errno));
    }

    // Setup target address
    memset(&forwarder->target_addr, 0, sizeof(forwarder->target_addr));
    forwarder->target_addr.sin_family = AF_INET;
    forwarder->target_addr.sin_port = htons(target_port);

    // Resolve hostname
    if (inet_aton(target_host, &forwarder->target_addr.sin_addr) == 0) {
        if (resolve_hostname(target_host, &forwarder->target_addr) < 0) {
            close(forwarder->udp_socket);
            free(forwarder);
            return NULL;
        }
    }

    // Initialize mutex
    if (pthread_mutex_init(&forwarder->send_mutex, NULL) != 0) {
        fprintf(stderr, "Failed to initialize send mutex\n");
        close(forwarder->udp_socket);
        free(forwarder);
        return NULL;
    }

    printf("UDP forwarder created for target %s:%d\n", target_host, target_port);
    return forwarder;
}

void udp_forwarder_destroy(udp_forwarder_t* forwarder) {
    if (!forwarder) return;

    // Stop threads if running
    if (forwarder->running) {
        udp_forwarder_stop(forwarder);
    }

    // Close socket
    if (forwarder->udp_socket >= 0) {
        close(forwarder->udp_socket);
    }

    // Destroy mutex
    pthread_mutex_destroy(&forwarder->send_mutex);

    free(forwarder);
    printf("UDP forwarder destroyed\n");
}

int udp_forwarder_start(udp_forwarder_t* forwarder) {
    if (!forwarder || forwarder->running) {
        return -1;
    }

    forwarder->running = 1;

    // Start receiver thread
    if (pthread_create(&forwarder->receiver_thread, NULL, 
                      udp_forwarder_receiver_thread, forwarder) != 0) {
        fprintf(stderr, "Failed to create receiver thread: %s\n", strerror(errno));
        forwarder->running = 0;
        return -1;
    }

    printf("UDP forwarder started\n");
    return 0;
}

void udp_forwarder_stop(udp_forwarder_t* forwarder) {
    if (!forwarder || !forwarder->running) {
        return;
    }

    forwarder->running = 0;

    // Wait for receiver thread to finish
    pthread_join(forwarder->receiver_thread, NULL);

    printf("UDP forwarder stopped\n");
}

int udp_forwarder_send(udp_forwarder_t* forwarder, 
                      uint32_t client_id,
                      const void* data, 
                      size_t size) {
    if (!forwarder || !data || size == 0 || size > UDP_BUFFER_SIZE) {
        return -1;
    }

    // Find client to update statistics
    client_entry_t* client = client_table_find(forwarder->clients, client_id);
    if (!client) {
        fprintf(stderr, "Client %u not found in table\n", client_id);
        return -1;
    }

    pthread_mutex_lock(&forwarder->send_mutex);

    int bytes_sent = -1;
    for (int retry = 0; retry < MAX_RETRIES; retry++) {
        bytes_sent = sendto(forwarder->udp_socket, data, size, 0,
                           (struct sockaddr*)&forwarder->target_addr,
                           sizeof(forwarder->target_addr));
        
        if (bytes_sent >= 0) {
            break;
        }
        
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            break;
        }
        
        // Small delay before retry
        usleep(RETRY_DELAY_MS * 1000);
    }

    if (bytes_sent > 0) {
        // Update statistics
        forwarder->total_packets_sent++;
        forwarder->total_bytes_sent += bytes_sent;
        
        // Update client statistics
        client->bytes_sent += bytes_sent;
        client_table_update_activity(forwarder->clients, client_id);
    } else {
        fprintf(stderr, "Failed to send UDP packet to target: %s\n", strerror(errno));
    }

    pthread_mutex_unlock(&forwarder->send_mutex);
    return bytes_sent;
}

void* udp_forwarder_receiver_thread(void* arg) {
    udp_forwarder_t* forwarder = (udp_forwarder_t*)arg;
    char buffer[UDP_BUFFER_SIZE];
    struct sockaddr_in sender_addr;
    socklen_t sender_len = sizeof(sender_addr);
    fd_set read_fds;
    struct timeval timeout;

    printf("UDP forwarder receiver thread started\n");

    while (forwarder->running) {
        FD_ZERO(&read_fds);
        FD_SET(forwarder->udp_socket, &read_fds);
        
        // Set timeout for select
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;

        int activity = select(forwarder->udp_socket + 1, &read_fds, NULL, NULL, &timeout);
        
        if (activity < 0) {
            if (errno != EINTR) {
                fprintf(stderr, "Select error in UDP receiver: %s\n", strerror(errno));
                break;
            }
            continue;
        }
        
        if (activity == 0) {
            // Timeout, continue loop
            continue;
        }

        if (FD_ISSET(forwarder->udp_socket, &read_fds)) {
            ssize_t bytes_received = recvfrom(forwarder->udp_socket, buffer, 
                                            sizeof(buffer), 0,
                                            (struct sockaddr*)&sender_addr,
                                            &sender_len);
            
            if (bytes_received > 0) {
                // Update statistics
                forwarder->total_packets_received++;
                forwarder->total_bytes_received += bytes_received;

                // For now, we'll broadcast the response to all clients
                // In a more sophisticated implementation, we would need to 
                // maintain a mapping of UDP requests to clients
                
                // Get all active clients and send response
                client_entry_t* current = NULL;
                pthread_mutex_lock(&forwarder->clients->mutex);
                
                current = forwarder->clients->head;
                while (current) {
                    if (current->tcp_socket >= 0) {
                        // Send response back to this client
                        udp_forwarder_send_response(forwarder, current->client_id,
                                                  buffer, bytes_received);
                    }
                    current = current->next;
                }
                
                pthread_mutex_unlock(&forwarder->clients->mutex);
                
            } else if (bytes_received < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
                fprintf(stderr, "Error receiving UDP data: %s\n", strerror(errno));
            }
        }
    }

    printf("UDP forwarder receiver thread stopped\n");
    return NULL;
}

int udp_forwarder_send_response(udp_forwarder_t* forwarder,
                               uint32_t client_id,
                               const void* data,
                               size_t size) {
    if (!forwarder || !data || size == 0) {
        return -1;
    }

    // Find client
    client_entry_t* client = client_table_find(forwarder->clients, client_id);
    if (!client || client->tcp_socket < 0) {
        return -1;
    }

    // Create protocol message
    char message_buffer[UDP_BUFFER_SIZE + UDP_BRIDGE_HEADER_SIZE];
    int message_size = protocol_create_message(message_buffer, sizeof(message_buffer), 
                                             MSG_DATA, client_id, FLAG_NONE, 
                                             data, size);
    
    if (message_size < 0) {
        fprintf(stderr, "Failed to create protocol message\n");
        return -1;
    }

    // Send through TCP socket
    ssize_t bytes_sent = send(client->tcp_socket, message_buffer, message_size, MSG_NOSIGNAL);
    
    if (bytes_sent > 0) {
        // Update client statistics
        client->bytes_received += size;
        client_table_update_activity(forwarder->clients, client_id);
        return 0;
    } else {
        fprintf(stderr, "Failed to send response to client %u: %s\n", 
                client_id, strerror(errno));
        return -1;
    }
}

void udp_forwarder_get_stats(udp_forwarder_t* forwarder,
                            uint64_t* packets_sent,
                            uint64_t* packets_received,
                            uint64_t* bytes_sent,
                            uint64_t* bytes_received,
                            time_t* uptime_seconds) {
    if (!forwarder) return;

    if (packets_sent) *packets_sent = forwarder->total_packets_sent;
    if (packets_received) *packets_received = forwarder->total_packets_received;
    if (bytes_sent) *bytes_sent = forwarder->total_bytes_sent;
    if (bytes_received) *bytes_received = forwarder->total_bytes_received;
    if (uptime_seconds) *uptime_seconds = time(NULL) - forwarder->start_time;
}

void udp_forwarder_print_stats(udp_forwarder_t* forwarder) {
    if (!forwarder) return;

    time_t uptime = time(NULL) - forwarder->start_time;
    
    printf("\n=== UDP Forwarder Statistics ===\n");
    printf("Uptime: %ld seconds\n", uptime);
    printf("Packets sent: %lu\n", forwarder->total_packets_sent);
    printf("Packets received: %lu\n", forwarder->total_packets_received);
    printf("Bytes sent: %lu\n", forwarder->total_bytes_sent);
    printf("Bytes received: %lu\n", forwarder->total_bytes_received);
    printf("Target: %s:%d\n", 
           inet_ntoa(forwarder->target_addr.sin_addr),
           ntohs(forwarder->target_addr.sin_port));
    printf("Status: %s\n", forwarder->running ? "Running" : "Stopped");
    printf("===============================\n\n");
}
