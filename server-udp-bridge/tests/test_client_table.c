#include "../src/client_table.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <assert.h>

void test_basic_operations() {
    printf("=== Testing Basic Operations ===\n");
    
    // Create table
    client_table_t* table = client_table_create(10, 30);
    assert(table != NULL);
    assert(client_table_get_count(table) == 0);
    
    // Create dummy sockets for testing
    int sockets[3];
    for (int i = 0; i < 3; i++) {
        sockets[i] = socket(AF_INET, SOCK_STREAM, 0);
        assert(sockets[i] > 0);
    }
    
    // Add clients
    uint32_t client1 = client_table_add(table, sockets[0]);
    uint32_t client2 = client_table_add(table, sockets[1]);
    uint32_t client3 = client_table_add(table, sockets[2]);
    
    assert(client1 > 0);
    assert(client2 > 0);
    assert(client3 > 0);
    assert(client1 != client2);
    assert(client2 != client3);
    assert(client_table_get_count(table) == 3);
    
    // Find clients
    client_entry_t* entry1 = client_table_find(table, client1);
    assert(entry1 != NULL);
    assert(entry1->client_id == client1);
    assert(entry1->tcp_socket == sockets[0]);
    
    // Update activity and stats
    client_table_update_activity(table, client1);
    client_table_update_stats(table, client1, 100, 200);
    
    entry1 = client_table_find(table, client1);
    assert(entry1->bytes_received == 100);
    assert(entry1->bytes_sent == 200);
    assert(entry1->packet_count == 1);
    
    // Print stats
    client_table_print_stats(table);
    
    // Remove a client
    assert(client_table_remove(table, client2) == 0);
    assert(client_table_get_count(table) == 2);
    assert(client_table_find(table, client2) == NULL);
    
    // Try to remove non-existent client
    assert(client_table_remove(table, 999) == -1);
    
    // Cleanup
    client_table_destroy(table);
    
    printf("✓ Basic operations test passed\n\n");
}

void test_limits() {
    printf("=== Testing Limits ===\n");
    
    // Create table with small limit
    client_table_t* table = client_table_create(2, 30);
    assert(table != NULL);
    
    // Create dummy sockets
    int sockets[5];
    for (int i = 0; i < 5; i++) {
        sockets[i] = socket(AF_INET, SOCK_STREAM, 0);
        assert(sockets[i] > 0);
    }
    
    // Add clients up to limit
    uint32_t client1 = client_table_add(table, sockets[0]);
    uint32_t client2 = client_table_add(table, sockets[1]);
    assert(client1 > 0);
    assert(client2 > 0);
    assert(client_table_get_count(table) == 2);
    
    // Try to exceed limit
    uint32_t client3 = client_table_add(table, sockets[2]);
    assert(client3 == 0);  // Should fail
    assert(client_table_get_count(table) == 2);
    
    // Remove one and try again
    assert(client_table_remove(table, client1) == 0);
    client3 = client_table_add(table, sockets[3]);
    assert(client3 > 0);
    assert(client_table_get_count(table) == 2);
    
    // Close unused sockets
    for (int i = 2; i < 5; i++) {
        if (i != 3) close(sockets[i]);
    }
    
    client_table_destroy(table);
    
    printf("✓ Limits test passed\n\n");
}

void test_cleanup() {
    printf("=== Testing Cleanup ===\n");
    
    // Create table with very short timeout for testing
    client_table_t* table = client_table_create(10, 2);  // 2 second timeout
    assert(table != NULL);
    
    // Create dummy sockets
    int sockets[3];
    for (int i = 0; i < 3; i++) {
        sockets[i] = socket(AF_INET, SOCK_STREAM, 0);
        assert(sockets[i] > 0);
    }
    
    // Add clients
    uint32_t client1 = client_table_add(table, sockets[0]);
    uint32_t client2 = client_table_add(table, sockets[1]);
    uint32_t client3 = client_table_add(table, sockets[2]);
    
    assert(client_table_get_count(table) == 3);
    
    printf("Waiting 3 seconds for clients to expire...\n");
    sleep(3);
    
    // Update activity for client1 after sleep to keep it alive
    client_table_update_activity(table, client1);
    
    // Manual cleanup test
    int removed = client_table_cleanup_expired(table);
    printf("Removed %d expired clients\n", removed);
    assert(removed == 2);  // client2 and client3 should be expired
    assert(client_table_get_count(table) == 1);
    
    // Verify only client1 remains
    assert(client_table_find(table, client1) != NULL);
    assert(client_table_find(table, client2) == NULL);
    assert(client_table_find(table, client3) == NULL);
    
    client_table_destroy(table);
    
    printf("✓ Cleanup test passed\n\n");
}

void test_background_cleanup() {
    printf("=== Testing Background Cleanup ===\n");
    
    // Create table with short timeout
    client_table_t* table = client_table_create(10, 2);
    assert(table != NULL);
    
    // Start background cleanup
    assert(client_table_start_cleanup(table) == 0);
    
    // Create dummy sockets
    int sockets[2];
    for (int i = 0; i < 2; i++) {
        sockets[i] = socket(AF_INET, SOCK_STREAM, 0);
        assert(sockets[i] > 0);
    }
    
    // Add clients
    uint32_t client1 = client_table_add(table, sockets[0]);
    uint32_t client2 = client_table_add(table, sockets[1]);
    
    assert(client1 > 0);
    assert(client2 > 0);
    assert(client_table_get_count(table) == 2);
    
    printf("Waiting 4 seconds for background cleanup...\n");
    sleep(4);
    
    // Background cleanup should have removed expired clients
    assert(client_table_get_count(table) == 0);
    
    client_table_destroy(table);  // This will stop cleanup thread
    
    printf("✓ Background cleanup test passed\n\n");
}

int main() {
    printf("Starting Client Table Tests\n");
    printf("==========================\n\n");
    
    test_basic_operations();
    test_limits();
    test_cleanup();
    test_background_cleanup();
    
    printf("🎉 All tests passed!\n");
    return 0;
}
