#include "src/client_table.h"
#include "src/udp_forwarder.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main()
{
    printf("Simple UDP Forwarder Test\n");
    printf("========================\n");

    // Create client table
    printf("1. Creating client table...\n");
    client_table_t *clients = client_table_create(10, 300);
    if (!clients) {
        fprintf(stderr, "Failed to create client table\n");
        return 1;
    }
    printf("   ✓ Client table created\n");

    // Create UDP forwarder (target doesn't need to exist for basic test)
    printf("2. Creating UDP forwarder...\n");
    udp_forwarder_t *forwarder = udp_forwarder_create("127.0.0.1", 5060, clients);
    if (!forwarder) {
        fprintf(stderr, "Failed to create UDP forwarder\n");
        client_table_destroy(clients);
        return 1;
    }
    printf("   ✓ UDP forwarder created\n");

    // Test starting forwarder
    printf("3. Starting UDP forwarder...\n");
    if (udp_forwarder_start(forwarder) < 0) {
        fprintf(stderr, "Failed to start UDP forwarder\n");
        udp_forwarder_destroy(forwarder);
        client_table_destroy(clients);
        return 1;
    }
    printf("   ✓ UDP forwarder started\n");

    // Add test client
    printf("4. Adding test client...\n");
    int dummy_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (dummy_socket < 0) {
        fprintf(stderr, "Failed to create dummy socket\n");
        udp_forwarder_destroy(forwarder);
        client_table_destroy(clients);
        return 1;
    }

    uint32_t client_id = client_table_add(clients, dummy_socket);
    if (client_id == 0) {
        fprintf(stderr, "Failed to add test client\n");
        close(dummy_socket);
        udp_forwarder_destroy(forwarder);
        client_table_destroy(clients);
        return 1;
    }
    printf("   ✓ Test client added with ID: %u\n", client_id);

    // Test getting statistics
    printf("5. Getting statistics...\n");
    udp_forwarder_print_stats(forwarder);

    // Wait a moment
    printf("6. Running for 2 seconds...\n");
    sleep(2);

    // Test sending data (will fail since no target server, but that's ok)
    printf("7. Testing send function...\n");
    const char *test_data = "Test data";
    int result = udp_forwarder_send(forwarder, client_id, test_data, strlen(test_data));
    printf("   Send result: %d (expected to fail without target server)\n", result);

    // Final statistics
    printf("8. Final statistics:\n");
    udp_forwarder_print_stats(forwarder);

    // Cleanup
    printf("9. Cleaning up...\n");
    close(dummy_socket);
    udp_forwarder_destroy(forwarder);
    client_table_destroy(clients);

    printf("   ✓ All resources cleaned up\n");
    printf("\nSimple test completed successfully!\n");
    return 0;
}
