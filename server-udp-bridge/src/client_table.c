#include "client_table.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>

// Internal function prototypes
static void* cleanup_thread_func(void* arg);

client_table_t* client_table_create(uint32_t max_clients, time_t timeout_seconds) {
    if (max_clients == 0 || timeout_seconds <= 0) {
        fprintf(stderr, "Invalid parameters for client table creation\n");
        return NULL;
    }
    
    client_table_t* table = malloc(sizeof(client_table_t));
    if (!table) {
        perror("Failed to allocate client table");
        return NULL;
    }
    
    // Initialize table structure
    table->head = NULL;
    table->count = 0;
    table->next_id = 1;  // Start from 1, 0 is reserved for error
    table->max_clients = max_clients;
    table->timeout_seconds = timeout_seconds;
    table->cleanup_running = 0;
    
    // Initialize mutex
    if (pthread_mutex_init(&table->mutex, NULL) != 0) {
        perror("Failed to initialize mutex");
        free(table);
        return NULL;
    }
    
    printf("Client table created: max_clients=%u, timeout=%ld seconds\n", 
           max_clients, timeout_seconds);
    
    return table;
}

void client_table_destroy(client_table_t* table) {
    if (!table) return;
    
    // Stop cleanup thread if running
    client_table_stop_cleanup(table);
    
    // Lock mutex for cleanup
    pthread_mutex_lock(&table->mutex);
    
    // Free all client entries
    client_entry_t* current = table->head;
    while (current) {
        client_entry_t* next = current->next;
        
        // Close TCP socket if open
        if (current->tcp_socket > 0) {
            close(current->tcp_socket);
        }
        
        printf("Removing client %u (bytes_rx=%lu, bytes_tx=%lu)\n",
               current->client_id, current->bytes_received, current->bytes_sent);
        
        free(current);
        current = next;
    }
    
    pthread_mutex_unlock(&table->mutex);
    pthread_mutex_destroy(&table->mutex);
    
    printf("Client table destroyed\n");
    free(table);
}

uint32_t client_table_add(client_table_t* table, int tcp_socket) {
    if (!table || tcp_socket <= 0) {
        fprintf(stderr, "Invalid parameters for client add\n");
        return 0;
    }
    
    pthread_mutex_lock(&table->mutex);
    
    // Check if we've reached the maximum number of clients
    if (table->count >= table->max_clients) {
        fprintf(stderr, "Maximum number of clients reached (%u)\n", table->max_clients);
        pthread_mutex_unlock(&table->mutex);
        return 0;
    }
    
    // Allocate new client entry
    client_entry_t* new_client = malloc(sizeof(client_entry_t));
    if (!new_client) {
        perror("Failed to allocate new client entry");
        pthread_mutex_unlock(&table->mutex);
        return 0;
    }
    
    // Initialize client entry
    new_client->client_id = table->next_id++;
    new_client->last_activity = time(NULL);
    new_client->bytes_received = 0;
    new_client->bytes_sent = 0;
    new_client->packet_count = 0;
    new_client->tcp_socket = tcp_socket;
    new_client->next = table->head;
    
    // Add to the head of the list
    table->head = new_client;
    table->count++;
    
    uint32_t client_id = new_client->client_id;
    
    pthread_mutex_unlock(&table->mutex);
    
    printf("Added new client: ID=%u, socket=%d, total_clients=%u\n",
           client_id, tcp_socket, table->count);
    
    return client_id;
}

client_entry_t* client_table_find(client_table_t* table, uint32_t client_id) {
    if (!table || client_id == 0) return NULL;
    
    pthread_mutex_lock(&table->mutex);
    
    client_entry_t* current = table->head;
    while (current) {
        if (current->client_id == client_id) {
            pthread_mutex_unlock(&table->mutex);
            return current;
        }
        current = current->next;
    }
    
    pthread_mutex_unlock(&table->mutex);
    return NULL;
}

client_entry_t* client_table_find_by_socket(client_table_t* table, int tcp_socket) {
    if (!table || tcp_socket < 0) return NULL;
    
    pthread_mutex_lock(&table->mutex);
    
    client_entry_t* current = table->head;
    while (current) {
        if (current->tcp_socket == tcp_socket) {
            pthread_mutex_unlock(&table->mutex);
            return current;
        }
        current = current->next;
    }
    
    pthread_mutex_unlock(&table->mutex);
    return NULL;
}

int client_table_remove(client_table_t* table, uint32_t client_id) {
    if (!table || client_id == 0) return -1;
    
    pthread_mutex_lock(&table->mutex);
    
    client_entry_t* current = table->head;
    client_entry_t* prev = NULL;
    
    while (current) {
        if (current->client_id == client_id) {
            // Remove from list
            if (prev) {
                prev->next = current->next;
            } else {
                table->head = current->next;
            }
            
            // Close TCP socket
            if (current->tcp_socket > 0) {
                close(current->tcp_socket);
            }
            
            printf("Removed client %u (bytes_rx=%lu, bytes_tx=%lu, packets=%u)\n",
                   current->client_id, current->bytes_received, 
                   current->bytes_sent, current->packet_count);
            
            free(current);
            table->count--;
            
            pthread_mutex_unlock(&table->mutex);
            return 0;
        }
        prev = current;
        current = current->next;
    }
    
    pthread_mutex_unlock(&table->mutex);
    return -1;  // Client not found
}

void client_table_update_activity(client_table_t* table, uint32_t client_id) {
    if (!table || client_id == 0) return;
    
    pthread_mutex_lock(&table->mutex);
    
    client_entry_t* client = table->head;
    while (client) {
        if (client->client_id == client_id) {
            client->last_activity = time(NULL);
            client->packet_count++;
            break;
        }
        client = client->next;
    }
    
    pthread_mutex_unlock(&table->mutex);
}

void client_table_update_stats(client_table_t* table, uint32_t client_id, 
                              uint64_t bytes_received, uint64_t bytes_sent) {
    if (!table || client_id == 0) return;
    
    pthread_mutex_lock(&table->mutex);
    
    client_entry_t* client = table->head;
    while (client) {
        if (client->client_id == client_id) {
            client->bytes_received += bytes_received;
            client->bytes_sent += bytes_sent;
            client->last_activity = time(NULL);
            break;
        }
        client = client->next;
    }
    
    pthread_mutex_unlock(&table->mutex);
}

int client_table_cleanup_expired(client_table_t* table) {
    if (!table) return 0;
    
    pthread_mutex_lock(&table->mutex);
    
    time_t current_time = time(NULL);
    int removed_count = 0;
    
    client_entry_t* current = table->head;
    client_entry_t* prev = NULL;
    
    while (current) {
        time_t age = current_time - current->last_activity;
        
        if (age > table->timeout_seconds) {
            client_entry_t* to_remove = current;
            
            // Remove from list
            if (prev) {
                prev->next = current->next;
            } else {
                table->head = current->next;
            }
            
            current = current->next;
            
            // Close TCP socket
            if (to_remove->tcp_socket > 0) {
                close(to_remove->tcp_socket);
            }
            
            printf("Expired client %u (inactive for %ld seconds)\n",
                   to_remove->client_id, age);
            
            free(to_remove);
            table->count--;
            removed_count++;
        } else {
            prev = current;
            current = current->next;
        }
    }
    
    pthread_mutex_unlock(&table->mutex);
    
    if (removed_count > 0) {
        printf("Cleaned up %d expired clients, remaining: %u\n", 
               removed_count, table->count);
    }
    
    return removed_count;
}

uint32_t client_table_get_count(client_table_t* table) {
    if (!table) return 0;
    
    pthread_mutex_lock(&table->mutex);
    uint32_t count = table->count;
    pthread_mutex_unlock(&table->mutex);
    
    return count;
}

void client_table_print_stats(client_table_t* table) {
    if (!table) return;
    
    pthread_mutex_lock(&table->mutex);
    
    printf("\n=== Client Table Statistics ===\n");
    printf("Active clients: %u / %u\n", table->count, table->max_clients);
    printf("Timeout: %ld seconds\n", table->timeout_seconds);
    printf("Next ID: %u\n", table->next_id);
    printf("Cleanup thread: %s\n", table->cleanup_running ? "Running" : "Stopped");
    
    if (table->count > 0) {
        printf("\nClient Details:\n");
        printf("%-8s %-8s %-12s %-12s %-8s %-8s\n",
               "ID", "Socket", "RX Bytes", "TX Bytes", "Packets", "Age");
        printf("--------------------------------------------------------------\n");
        
        time_t current_time = time(NULL);
        client_entry_t* current = table->head;
        
        while (current) {
            time_t age = current_time - current->last_activity;
            printf("%-8u %-8d %-12lu %-12lu %-8u %-8ld\n",
                   current->client_id,
                   current->tcp_socket,
                   current->bytes_received,
                   current->bytes_sent,
                   current->packet_count,
                   age);
            current = current->next;
        }
    }
    
    printf("===============================\n\n");
    
    pthread_mutex_unlock(&table->mutex);
}

int client_table_start_cleanup(client_table_t* table) {
    if (!table) return -1;
    
    pthread_mutex_lock(&table->mutex);
    
    if (table->cleanup_running) {
        pthread_mutex_unlock(&table->mutex);
        printf("Cleanup thread already running\n");
        return 0;
    }
    
    table->cleanup_running = 1;
    
    if (pthread_create(&table->cleanup_thread, NULL, cleanup_thread_func, table) != 0) {
        perror("Failed to create cleanup thread");
        table->cleanup_running = 0;
        pthread_mutex_unlock(&table->mutex);
        return -1;
    }
    
    pthread_mutex_unlock(&table->mutex);
    
    printf("Client cleanup thread started\n");
    return 0;
}

void client_table_stop_cleanup(client_table_t* table) {
    if (!table) return;
    
    pthread_mutex_lock(&table->mutex);
    
    if (!table->cleanup_running) {
        pthread_mutex_unlock(&table->mutex);
        return;
    }
    
    table->cleanup_running = 0;
    pthread_mutex_unlock(&table->mutex);
    
    // Wait for thread to finish
    pthread_join(table->cleanup_thread, NULL);
    
    printf("Client cleanup thread stopped\n");
}

// Internal functions

static void* cleanup_thread_func(void* arg) {
    client_table_t* table = (client_table_t*)arg;
    time_t sleep_interval = table->timeout_seconds / 2;
    
    // Ensure minimum sleep interval of 1 second, maximum 10 seconds
    if (sleep_interval < 1) sleep_interval = 1;
    if (sleep_interval > 10) sleep_interval = 10;
    
    printf("Cleanup thread started, checking every %ld seconds\n", sleep_interval);
    
    while (table->cleanup_running) {
        // Use shorter sleep intervals for faster shutdown
        for (int i = 0; i < sleep_interval && table->cleanup_running; i++) {
            sleep(1);
        }
        
        if (!table->cleanup_running) break;
        
        // Clean up expired clients
        client_table_cleanup_expired(table);
    }
    
    printf("Cleanup thread exiting\n");
    return NULL;
}
