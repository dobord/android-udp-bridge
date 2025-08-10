#include "client_table.h"
#include "protocol.h"
#include "udp_forwarder.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>

// Global variables for graceful shutdown
static volatile int running = 1;
static client_table_t* global_client_table = NULL;

void signal_handler(int sig) {
    (void)sig;
    printf("\nReceived shutdown signal, cleaning up...\n");
    running = 0;
}

void print_usage(const char* program_name) {
    printf("Usage: %s [options]\n", program_name);
    printf("Options:\n");
    printf("  -p <port>        TCP port to listen on (default: 8080)\n");
    printf("  -t <host:port>   Target UDP server (default: localhost:5060)\n");
    printf("  -m <max>         Maximum clients (default: 1000)\n");
    printf("  -T <timeout>     Client timeout in seconds (default: 300)\n");
    printf("  -h               Show this help\n");
}

int parse_target(const char* target, char* host, int* port) {
    char* colon = strchr(target, ':');
    if (!colon) {
        return -1;
    }
    
    size_t host_len = colon - target;
    if (host_len >= 256) {
        return -1;
    }
    
    strncpy(host, target, host_len);
    host[host_len] = '\0';
    
    *port = atoi(colon + 1);
    if (*port <= 0 || *port > 65535) {
        return -1;
    }
    
    return 0;
}

int main(int argc, char* argv[]) {
    // Default configuration
    int listen_port = 8080;
    char target_host[256] = "localhost";
    int target_port = 5060;
    uint32_t max_clients = 1000;
    time_t client_timeout = 300;
    
    // Parse command line arguments
    int opt;
    while ((opt = getopt(argc, argv, "p:t:m:T:h")) != -1) {
        switch (opt) {
            case 'p':
                listen_port = atoi(optarg);
                if (listen_port <= 0 || listen_port > 65535) {
                    fprintf(stderr, "Invalid port number: %s\n", optarg);
                    return 1;
                }
                break;
            case 't':
                if (parse_target(optarg, target_host, &target_port) != 0) {
                    fprintf(stderr, "Invalid target format: %s (expected host:port)\n", optarg);
                    return 1;
                }
                break;
            case 'm':
                max_clients = atoi(optarg);
                if (max_clients == 0) {
                    fprintf(stderr, "Invalid max clients: %s\n", optarg);
                    return 1;
                }
                break;
            case 'T':
                client_timeout = atoi(optarg);
                if (client_timeout <= 0) {
                    fprintf(stderr, "Invalid timeout: %s\n", optarg);
                    return 1;
                }
                break;
            case 'h':
                print_usage(argv[0]);
                return 0;
            default:
                print_usage(argv[0]);
                return 1;
        }
    }
    
    printf("=== UDP Bridge Server ===\n");
    printf("Listen port: %d\n", listen_port);
    printf("Target: %s:%d\n", target_host, target_port);
    printf("Max clients: %u\n", max_clients);
    printf("Client timeout: %ld seconds\n", client_timeout);
    printf("=========================\n\n");
    
    // Setup signal handling
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // Initialize client table
    global_client_table = client_table_create(max_clients, client_timeout);
    if (!global_client_table) {
        fprintf(stderr, "Failed to create client table\n");
        return 1;
    }
    
    // Start background cleanup
    if (client_table_start_cleanup(global_client_table) != 0) {
        fprintf(stderr, "Failed to start cleanup thread\n");
        client_table_destroy(global_client_table);
        return 1;
    }
    
    // Create TCP server socket
    int server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0) {
        perror("Failed to create server socket");
        client_table_destroy(global_client_table);
        return 1;
    }
    
    // Set socket options
    int opt_val = 1;
    if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt_val, sizeof(opt_val)) < 0) {
        perror("Failed to set socket options");
        close(server_socket);
        client_table_destroy(global_client_table);
        return 1;
    }
    
    // Bind socket
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(listen_port);
    
    if (bind(server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Failed to bind socket");
        close(server_socket);
        client_table_destroy(global_client_table);
        return 1;
    }
    
    // Listen for connections
    if (listen(server_socket, 10) < 0) {
        perror("Failed to listen on socket");
        close(server_socket);
        client_table_destroy(global_client_table);
        return 1;
    }
    
    printf("Server listening on port %d...\n", listen_port);
    printf("Press Ctrl+C to stop\n\n");
    
    // Initialize UDP forwarder (TODO: implement udp_forwarder module)
    /*
    udp_forwarder_t* forwarder = udp_forwarder_create(target_host, target_port, global_client_table);
    if (!forwarder) {
        fprintf(stderr, "Failed to create UDP forwarder\n");
        close(server_socket);
        client_table_destroy(global_client_table);
        return 1;
    }
    */
    printf("UDP forwarder: %s:%d (not implemented yet)\n", target_host, target_port);
    
    // Main server loop
    while (running) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        
        // Accept connection with timeout
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(server_socket, &read_fds);
        
        struct timeval timeout;
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;
        
        int activity = select(server_socket + 1, &read_fds, NULL, NULL, &timeout);
        
        if (activity < 0 && errno != EINTR) {
            perror("Select error");
            break;
        }
        
        if (activity == 0) {
            // Timeout - print stats every few seconds
            static int stats_counter = 0;
            if (++stats_counter >= 5) {
                printf("Active clients: %u\n", client_table_get_count(global_client_table));
                stats_counter = 0;
            }
            continue;
        }
        
        if (!FD_ISSET(server_socket, &read_fds)) {
            continue;
        }
        
        int client_socket = accept(server_socket, (struct sockaddr*)&client_addr, &client_len);
        if (client_socket < 0) {
            if (errno != EINTR) {
                perror("Failed to accept connection");
            }
            continue;
        }
        
        printf("New connection from %s:%d\n", 
               inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
        
        // Add client to table
        uint32_t client_id = client_table_add(global_client_table, client_socket);
        if (client_id == 0) {
            fprintf(stderr, "Failed to add client (table full?)\n");
            close(client_socket);
            continue;
        }
        
        printf("Assigned client ID: %u\n", client_id);
        
        // TODO: Here we would normally start a thread to handle this client
        // For now, just close the connection after a brief demo
        
        // Send a demo protocol message
        char demo_message[256];
        const char* demo_data = "Welcome to UDP Bridge Server!";
        int message_size = protocol_create_message(demo_message, sizeof(demo_message), MSG_DATA, client_id, 
                                                  0, demo_data, strlen(demo_data));
        
        if (message_size > 0) {
            ssize_t sent = send(client_socket, demo_message, message_size, 0);
            if (sent > 0) {
                printf("Sent demo message to client %u\n", client_id);
            }
        }
        
        // Close connection after demo (in real implementation, keep it open)
        sleep(1);
        client_table_remove(global_client_table, client_id);
    }
    
    printf("\nShutting down server...\n");
    
    // Cleanup
    close(server_socket);
    // udp_forwarder_destroy(forwarder);  // TODO: implement
    client_table_destroy(global_client_table);
    
    printf("Server shutdown complete\n");
    return 0;
}
