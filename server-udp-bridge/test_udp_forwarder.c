#include "src/client_table.h"
#include "src/udp_forwarder.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static volatile int running = 1;

void signal_handler(int sig)
{
    printf("\nReceived signal %d, shutting down...\n", sig);
    running = 0;
}

int main(int argc, char *argv[])
{
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <target_host> <target_port>\n", argv[0]);
        return 1;
    }

    const char *target_host = argv[1];
    int target_port = atoi(argv[2]);
    int dummy_socket = -1;

    if (target_port <= 0 || target_port > 65535) {
        fprintf(stderr, "Invalid port number: %s\n", argv[2]);
        return 1;
    }

    // Setup signal handlers
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    printf("Testing UDP forwarder with target %s:%d\n", target_host, target_port);

    // Create client table
    client_table_t *clients = client_table_create(100, 300);
    if (!clients) {
        fprintf(stderr, "Failed to create client table\n");
        return 1;
    }

    // Create UDP forwarder
    udp_forwarder_t *forwarder = udp_forwarder_create(target_host, target_port, clients);
    if (!forwarder) {
        fprintf(stderr, "Failed to create UDP forwarder\n");
        client_table_destroy(clients);
        return 1;
    }

    // Start forwarder
    if (udp_forwarder_start(forwarder) < 0) {
        fprintf(stderr, "Failed to start UDP forwarder\n");
        udp_forwarder_destroy(forwarder);
        client_table_destroy(clients);
        return 1;
    }

    // Add a test client with a dummy socket for testing
    dummy_socket = socket(AF_INET, SOCK_STREAM, 0); // Create dummy socket for test
    uint32_t test_client_id = client_table_add(clients, dummy_socket);
    printf("Added test client with ID: %u\n", test_client_id);

    // Test sending some data
    const char *test_data = "Hello from UDP forwarder test!";
    printf("Sending test data: %s\n", test_data);

    int result = udp_forwarder_send(forwarder, test_client_id, test_data, strlen(test_data));
    if (result > 0) {
        printf("Successfully sent %d bytes\n", result);
    } else {
        printf("Failed to send test data\n");
    }

    // Run for a while and print stats
    int stats_counter = 0;
    while (running) {
        sleep(1);
        stats_counter++;

        if (stats_counter % 10 == 0) {
            udp_forwarder_print_stats(forwarder);
        }
    }

    printf("Cleaning up...\n");

    // Close dummy socket
    if (dummy_socket >= 0) {
        close(dummy_socket);
    }

    // Cleanup
    udp_forwarder_destroy(forwarder);
    client_table_destroy(clients);

    printf("Test completed\n");
    return 0;
}
