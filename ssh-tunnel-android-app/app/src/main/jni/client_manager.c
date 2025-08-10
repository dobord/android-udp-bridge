#include "client_manager.h"
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <arpa/inet.h>

// Helper function to compare sockaddr_in structures
static int sockaddr_equal(const struct sockaddr_in* a, const struct sockaddr_in* b) {
    return (a->sin_addr.s_addr == b->sin_addr.s_addr && a->sin_port == b->sin_port);
}

// Helper function to generate next client ID
static uint32_t generate_client_id(client_manager_t* manager) {
    uint32_t id = ++manager->next_client_id;
    // Avoid ID 0 and handle overflow
    if (id == 0) {
        id = ++manager->next_client_id;
    }
    return id;
}

// Helper function to create new client entry
static client_entry_t* create_client_entry(uint32_t client_id, const struct sockaddr_in* addr) {
    client_entry_t* entry = (client_entry_t*)malloc(sizeof(client_entry_t));
    if (!entry) {
        CM_LOGE("Failed to allocate memory for client entry");
        return NULL;
    }
    
    memset(entry, 0, sizeof(client_entry_t));
    entry->client_id = client_id;
    entry->client_addr = *addr;
    entry->created_time = time(NULL);
    entry->last_activity = entry->created_time;
    entry->state_flags = CLIENT_STATE_ACTIVE;
    entry->next = NULL;
    
    CM_LOGD("Created client entry: ID=%u, addr=%s:%d", 
           client_id, inet_ntoa(addr->sin_addr), ntohs(addr->sin_port));
    
    return entry;
}

client_manager_t* client_manager_create(time_t timeout, uint32_t max_clients) {
    client_manager_t* manager = (client_manager_t*)malloc(sizeof(client_manager_t));
    if (!manager) {
        CM_LOGE("Failed to allocate memory for client manager");
        return NULL;
    }
    
    memset(manager, 0, sizeof(client_manager_t));
    
    // Initialize configuration
    manager->client_timeout = (timeout > 0) ? timeout : CLIENT_TIMEOUT_SECONDS;
    manager->max_clients = (max_clients > 0) ? max_clients : MAX_CLIENTS;
    manager->last_cleanup = time(NULL);
    
    // Initialize mutex
    if (pthread_mutex_init(&manager->mutex, NULL) != 0) {
        CM_LOGE("Failed to initialize mutex: %s", strerror(errno));
        free(manager);
        return NULL;
    }
    
    CM_LOGI("Client manager created: timeout=%ld, max_clients=%u", 
           manager->client_timeout, manager->max_clients);
    
    return manager;
}

void client_manager_destroy(client_manager_t* manager) {
    if (!manager) return;
    
    pthread_mutex_lock(&manager->mutex);
    
    // Free all client entries
    client_entry_t* current = manager->head;
    while (current) {
        client_entry_t* next = current->next;
        free(current);
        current = next;
    }
    
    uint32_t final_count = manager->client_count;
    pthread_mutex_unlock(&manager->mutex);
    
    pthread_mutex_destroy(&manager->mutex);
    free(manager);
    
    CM_LOGI("Client manager destroyed, cleaned up %u clients", final_count);
}

uint32_t client_manager_add_client(client_manager_t* manager, const struct sockaddr_in* addr) {
    if (!manager || !addr) {
        CM_LOGE("Invalid parameters for add_client");
        return 0;
    }
    
    pthread_mutex_lock(&manager->mutex);
    
    // Check if client already exists
    client_entry_t* existing = NULL;
    client_entry_t* current = manager->head;
    while (current) {
        if (sockaddr_equal(&current->client_addr, addr)) {
            existing = current;
            break;
        }
        current = current->next;
    }
    
    if (existing) {
        // Update existing client
        existing->last_activity = time(NULL);
        existing->state_flags = CLIENT_STATE_ACTIVE;
        uint32_t client_id = existing->client_id;
        
        pthread_mutex_unlock(&manager->mutex);
        
        CM_LOGD("Updated existing client: ID=%u, addr=%s:%d", 
               client_id, inet_ntoa(addr->sin_addr), ntohs(addr->sin_port));
        return client_id;
    }
    
    // Check client limit
    if (manager->client_count >= manager->max_clients) {
        pthread_mutex_unlock(&manager->mutex);
        CM_LOGW("Maximum client limit reached: %u", manager->max_clients);
        return 0;
    }
    
    // Create new client
    uint32_t client_id = generate_client_id(manager);
    client_entry_t* entry = create_client_entry(client_id, addr);
    if (!entry) {
        pthread_mutex_unlock(&manager->mutex);
        return 0;
    }
    
    // Add to list
    entry->next = manager->head;
    manager->head = entry;
    manager->client_count++;
    manager->total_clients_created++;
    
    pthread_mutex_unlock(&manager->mutex);
    
    CM_LOGI("Added new client: ID=%u, addr=%s:%d, total_clients=%u", 
           client_id, inet_ntoa(addr->sin_addr), ntohs(addr->sin_port), 
           manager->client_count);
    
    return client_id;
}

client_entry_t* client_manager_find_by_id(client_manager_t* manager, uint32_t client_id) {
    if (!manager || client_id == 0) return NULL;
    
    pthread_mutex_lock(&manager->mutex);
    
    client_entry_t* current = manager->head;
    while (current) {
        if (current->client_id == client_id) {
            pthread_mutex_unlock(&manager->mutex);
            return current;
        }
        current = current->next;
    }
    
    pthread_mutex_unlock(&manager->mutex);
    return NULL;
}

client_entry_t* client_manager_find_by_addr(client_manager_t* manager, const struct sockaddr_in* addr) {
    if (!manager || !addr) return NULL;
    
    pthread_mutex_lock(&manager->mutex);
    
    client_entry_t* current = manager->head;
    while (current) {
        if (sockaddr_equal(&current->client_addr, addr)) {
            pthread_mutex_unlock(&manager->mutex);
            return current;
        }
        current = current->next;
    }
    
    pthread_mutex_unlock(&manager->mutex);
    return NULL;
}

int client_manager_update_activity(client_manager_t* manager, uint32_t client_id) {
    if (!manager || client_id == 0) return -1;
    
    pthread_mutex_lock(&manager->mutex);
    
    client_entry_t* client = manager->head;
    while (client) {
        if (client->client_id == client_id) {
            client->last_activity = time(NULL);
            client->state_flags |= CLIENT_STATE_ACTIVE;
            client->state_flags &= ~CLIENT_STATE_TIMEOUT;
            pthread_mutex_unlock(&manager->mutex);
            return 0;
        }
        client = client->next;
    }
    
    pthread_mutex_unlock(&manager->mutex);
    CM_LOGW("Client not found for activity update: ID=%u", client_id);
    return -1;
}

int client_manager_update_stats(client_manager_t* manager, uint32_t client_id, 
                               uint32_t bytes_received, uint32_t bytes_sent) {
    if (!manager || client_id == 0) return -1;
    
    pthread_mutex_lock(&manager->mutex);
    
    client_entry_t* client = manager->head;
    while (client) {
        if (client->client_id == client_id) {
            if (bytes_received > 0) {
                client->packets_received++;
                client->bytes_received += bytes_received;
            }
            if (bytes_sent > 0) {
                client->packets_sent++;
                client->bytes_sent += bytes_sent;
            }
            client->last_activity = time(NULL);
            manager->total_packets_processed++;
            pthread_mutex_unlock(&manager->mutex);
            return 0;
        }
        client = client->next;
    }
    
    pthread_mutex_unlock(&manager->mutex);
    CM_LOGW("Client not found for stats update: ID=%u", client_id);
    return -1;
}

int client_manager_set_error(client_manager_t* manager, uint32_t client_id, uint32_t error_code) {
    if (!manager || client_id == 0) return -1;
    
    pthread_mutex_lock(&manager->mutex);
    
    client_entry_t* client = manager->head;
    while (client) {
        if (client->client_id == client_id) {
            client->error_count++;
            client->last_error_code = error_code;
            client->state_flags |= CLIENT_STATE_ERROR;
            pthread_mutex_unlock(&manager->mutex);
            
            CM_LOGW("Client error set: ID=%u, error_code=%u, total_errors=%u", 
                   client_id, error_code, client->error_count);
            return 0;
        }
        client = client->next;
    }
    
    pthread_mutex_unlock(&manager->mutex);
    CM_LOGW("Client not found for error update: ID=%u", client_id);
    return -1;
}

int client_manager_cleanup_expired(client_manager_t* manager) {
    if (!manager) return -1;
    
    time_t now = time(NULL);
    int removed_count = 0;
    
    pthread_mutex_lock(&manager->mutex);
    
    client_entry_t* prev = NULL;
    client_entry_t* current = manager->head;
    
    while (current) {
        time_t age = now - current->last_activity;
        
        if (age > manager->client_timeout) {
            client_entry_t* to_remove = current;
            
            if (prev) {
                prev->next = current->next;
            } else {
                manager->head = current->next;
            }
            
            current = current->next;
            
            CM_LOGD("Removing expired client: ID=%u, age=%ld seconds", 
                   to_remove->client_id, age);
            
            free(to_remove);
            manager->client_count--;
            manager->total_clients_expired++;
            removed_count++;
        } else {
            prev = current;
            current = current->next;
        }
    }
    
    manager->last_cleanup = now;
    pthread_mutex_unlock(&manager->mutex);
    
    if (removed_count > 0) {
        CM_LOGI("Cleanup completed: removed %d expired clients, remaining: %u", 
               removed_count, manager->client_count);
    }
    
    return removed_count;
}

int client_manager_remove_client(client_manager_t* manager, uint32_t client_id) {
    if (!manager || client_id == 0) return -1;
    
    pthread_mutex_lock(&manager->mutex);
    
    client_entry_t* prev = NULL;
    client_entry_t* current = manager->head;
    
    while (current) {
        if (current->client_id == client_id) {
            if (prev) {
                prev->next = current->next;
            } else {
                manager->head = current->next;
            }
            
            free(current);
            manager->client_count--;
            pthread_mutex_unlock(&manager->mutex);
            
            CM_LOGI("Removed client: ID=%u, remaining: %u", client_id, manager->client_count);
            return 0;
        }
        prev = current;
        current = current->next;
    }
    
    pthread_mutex_unlock(&manager->mutex);
    CM_LOGW("Client not found for removal: ID=%u", client_id);
    return -1;
}

uint32_t client_manager_get_count(client_manager_t* manager) {
    if (!manager) return 0;
    
    pthread_mutex_lock(&manager->mutex);
    uint32_t count = manager->client_count;
    pthread_mutex_unlock(&manager->mutex);
    
    return count;
}

int client_manager_get_stats(client_manager_t* manager, uint32_t client_id,
                            uint64_t* packets_rx, uint64_t* packets_tx,
                            uint64_t* bytes_rx, uint64_t* bytes_tx) {
    if (!manager || client_id == 0) return -1;
    
    pthread_mutex_lock(&manager->mutex);
    
    client_entry_t* client = manager->head;
    while (client) {
        if (client->client_id == client_id) {
            if (packets_rx) *packets_rx = client->packets_received;
            if (packets_tx) *packets_tx = client->packets_sent;
            if (bytes_rx) *bytes_rx = client->bytes_received;
            if (bytes_tx) *bytes_tx = client->bytes_sent;
            pthread_mutex_unlock(&manager->mutex);
            return 0;
        }
        client = client->next;
    }
    
    pthread_mutex_unlock(&manager->mutex);
    return -1;
}

void client_manager_print_stats(client_manager_t* manager) {
    if (!manager) return;
    
    pthread_mutex_lock(&manager->mutex);
    
    CM_LOGI("=== Client Manager Statistics ===");
    CM_LOGI("Current clients: %u / %u", manager->client_count, manager->max_clients);
    CM_LOGI("Total created: %llu", (unsigned long long)manager->total_clients_created);
    CM_LOGI("Total expired: %llu", (unsigned long long)manager->total_clients_expired);
    CM_LOGI("Total packets: %llu", (unsigned long long)manager->total_packets_processed);
    CM_LOGI("Client timeout: %ld seconds", manager->client_timeout);
    
    time_t now = time(NULL);
    client_entry_t* client = manager->head;
    int client_num = 0;
    
    while (client && client_num < 10) {  // Limit output to first 10 clients
        time_t age = now - client->last_activity;
        CM_LOGI("Client[%d]: ID=%u, addr=%s:%d, age=%lds, rx=%llu, tx=%llu, errors=%u",
               client_num++, client->client_id,
               inet_ntoa(client->client_addr.sin_addr), 
               ntohs(client->client_addr.sin_port),
               age,
               (unsigned long long)client->packets_received,
               (unsigned long long)client->packets_sent,
               client->error_count);
        client = client->next;
    }
    
    if (client) {
        CM_LOGI("... and %u more clients", manager->client_count - client_num);
    }
    
    pthread_mutex_unlock(&manager->mutex);
}

int client_manager_auto_cleanup(client_manager_t* manager) {
    if (!manager) return -1;
    
    time_t now = time(NULL);
    
    // Check if cleanup is needed
    if ((now - manager->last_cleanup) < CLIENT_CLEANUP_INTERVAL) {
        return 0;  // No cleanup needed yet
    }
    
    return client_manager_cleanup_expired(manager);
}
