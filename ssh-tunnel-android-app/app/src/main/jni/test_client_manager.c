#include "client_manager.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

// Simple test for client_manager functionality
int main() {
    printf("=== Client Manager Test ===\n");
    
    // Test 1: Create client manager
    client_manager_t* manager = client_manager_create(60, 10);  // 60 sec timeout, max 10 clients
    assert(manager != NULL);
    printf("✓ Client manager created successfully\n");
    
    // Test 2: Add clients
    struct sockaddr_in addr1, addr2;
    memset(&addr1, 0, sizeof(addr1));
    memset(&addr2, 0, sizeof(addr2));
    
    addr1.sin_family = AF_INET;
    addr1.sin_addr.s_addr = htonl(0x7F000001);  // 127.0.0.1
    addr1.sin_port = htons(12345);
    
    addr2.sin_family = AF_INET;
    addr2.sin_addr.s_addr = htonl(0x7F000001);  // 127.0.0.1
    addr2.sin_port = htons(12346);
    
    uint32_t client1_id = client_manager_add_client(manager, &addr1);
    uint32_t client2_id = client_manager_add_client(manager, &addr2);
    
    assert(client1_id > 0);
    assert(client2_id > 0);
    assert(client1_id != client2_id);
    printf("✓ Added two clients: ID1=%u, ID2=%u\n", client1_id, client2_id);
    
    // Test 3: Check client count
    uint32_t count = client_manager_get_count(manager);
    assert(count == 2);
    printf("✓ Client count correct: %u\n", count);
    
    // Test 4: Find clients
    client_entry_t* found1 = client_manager_find_by_id(manager, client1_id);
    client_entry_t* found2 = client_manager_find_by_addr(manager, &addr2);
    
    assert(found1 != NULL);
    assert(found2 != NULL);
    assert(found1->client_id == client1_id);
    assert(found2->client_id == client2_id);
    printf("✓ Client lookup works\n");
    
    // Test 5: Update stats
    int result = client_manager_update_stats(manager, client1_id, 100, 200);
    assert(result == 0);
    
    uint64_t rx, tx, rx_bytes, tx_bytes;
    result = client_manager_get_stats(manager, client1_id, &rx, &tx, &rx_bytes, &tx_bytes);
    assert(result == 0);
    assert(rx == 1);    // 1 packet received
    assert(tx == 1);    // 1 packet sent  
    assert(rx_bytes == 100);
    assert(tx_bytes == 200);
    printf("✓ Client statistics work\n");
    
    // Test 6: Print stats
    client_manager_print_stats(manager);
    
    // Test 7: Remove client
    result = client_manager_remove_client(manager, client1_id);
    assert(result == 0);
    count = client_manager_get_count(manager);
    assert(count == 1);
    printf("✓ Client removal works\n");
    
    // Test 8: Cleanup
    client_manager_destroy(manager);
    printf("✓ Client manager destroyed\n");
    
    printf("=== All tests passed! ===\n");
    return 0;
}
