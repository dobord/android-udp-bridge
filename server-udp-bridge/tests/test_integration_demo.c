#include "../src/client_table.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <signal.h>

// Global variable for graceful shutdown
static volatile int running = 1;

void signal_handler(int sig) {
    (void)sig;
    printf("\nReceived shutdown signal, cleaning up...\n");
    running = 0;
}

int main() {
    // Setup signal handling
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    printf("=== Client Table Integration Demo ===\n");
    printf("Press Ctrl+C to stop\n\n");
    
    // Create client table with 100 max clients, 60 second timeout
    client_table_t* table = client_table_create(100, 60);
    if (!table) {
        fprintf(stderr, "Failed to create client table\n");
        return 1;
    }
    
    // Start background cleanup thread
    if (client_table_start_cleanup(table) != 0) {
        fprintf(stderr, "Failed to start cleanup thread\n");
        client_table_destroy(table);
        return 1;
    }
    
    printf("Client table initialized and cleanup thread started\n");
    
    // Simulate client connections and activity
    int demo_sockets[5];
    uint32_t client_ids[5];
    
    for (int i = 0; i < 5; i++) {
        // Create dummy socket for demo
        demo_sockets[i] = socket(AF_INET, SOCK_STREAM, 0);
        if (demo_sockets[i] < 0) {
            perror("Failed to create socket");
            continue;
        }
        
        // Add client to table
        client_ids[i] = client_table_add(table, demo_sockets[i]);
        if (client_ids[i] == 0) {
            fprintf(stderr, "Failed to add client %d\n", i);
            close(demo_sockets[i]);
            continue;
        }
        
        printf("Added demo client %d with ID %u\n", i, client_ids[i]);
        
        // Simulate some activity
        client_table_update_stats(table, client_ids[i], 1024 * (i + 1), 512 * (i + 1));
    }
    
    printf("\nInitial client statistics:\n");
    client_table_print_stats(table);
    
    // Main loop - simulate ongoing activity
    int loop_count = 0;
    while (running && loop_count < 30) {  // Run for 30 seconds max
        sleep(1);
        loop_count++;
        
        // Simulate activity for some clients
        for (int i = 0; i < 3; i++) {
            if (client_ids[i] > 0) {
                client_table_update_activity(table, client_ids[i]);
                client_table_update_stats(table, client_ids[i], 100, 50);
            }
        }
        
        // Print stats every 5 seconds
        if (loop_count % 5 == 0) {
            printf("\n--- After %d seconds ---\n", loop_count);
            client_table_print_stats(table);
        }
        
        // Remove a client after 10 seconds (simulating disconnect)
        if (loop_count == 10 && client_ids[4] > 0) {
            printf("Simulating client 4 disconnect...\n");
            client_table_remove(table, client_ids[4]);
            client_ids[4] = 0;
        }
    }
    
    printf("\nFinal statistics:\n");
    client_table_print_stats(table);
    
    // Cleanup
    printf("\nShutting down...\n");
    client_table_destroy(table);
    
    // Close any remaining sockets
    for (int i = 0; i < 5; i++) {
        if (demo_sockets[i] > 0 && client_ids[i] == 0) {
            close(demo_sockets[i]);
        }
    }
    
    printf("Demo completed successfully\n");
    return 0;
}
