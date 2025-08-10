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

// Function prototypes
void handle_client_connection(int client_socket, uint32_t client_id, 
                             udp_forwarder_t* forwarder, client_table_t* clients);

// Client connection handler
void handle_client_connection(int client_socket, uint32_t client_id, 
                             udp_forwarder_t* forwarder, client_table_t* clients) {
    char buffer[4096];
    int keepalive = 1;
    
    // Set socket to non-blocking for timeout handling
    struct timeval timeout;
    timeout.tv_sec = 30;  // 30 seconds timeout
    timeout.tv_usec = 0;
    setsockopt(client_socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    
    printf("Handling client %u connection\n", client_id);
    
    while (keepalive && running) {
        // Receive data from client
        ssize_t received = recv(client_socket, buffer, sizeof(buffer), 0);
        
        if (received <= 0) {
            if (received == 0) {
                printf("Client %u disconnected\n", client_id);
            } else {
                if (errno != EAGAIN && errno != EWOULDBLOCK) {
                    printf("Client %u receive error: %s\n", client_id, strerror(errno));
                }
            }
            break;
        }
        
        // Update client activity
        client_table_update_activity(clients, client_id);
        
        // Parse protocol message
        udp_bridge_header_t header;
        if (protocol_parse_header(buffer, received, &header) != 0) {
            printf("Client %u sent invalid protocol message\n", client_id);
            continue;
        }
        
        // Verify client ID matches
        if (header.client_id != client_id) {
            printf("Client %u sent message with wrong client_id %u\n", client_id, header.client_id);
            continue;
        }
        
        // Handle different message types
        switch (header.message_type) {
            case MSG_DATA: {
                // Extract UDP payload and forward it
                if (header.payload_size > 0 && received >= (ssize_t)(sizeof(header) + header.payload_size)) {
                    const char* payload = buffer + sizeof(header);
                    
                    printf("Client %u: forwarding %u bytes to UDP target\n", client_id, header.payload_size);
                    
                    // Forward to UDP target
                    int result = udp_forwarder_send(forwarder, client_id, payload, header.payload_size);
                    if (result < 0) {
                        printf("Failed to forward UDP data for client %u\n", client_id);
                    }
                } else {
                    printf("Client %u sent invalid MSG_DATA (payload_size=%u, received=%zd)\n", 
                           client_id, header.payload_size, received);
                }
                break;
            }
            
            case MSG_CLIENT_REGISTER: {
                printf("Client %u sent registration request\n", client_id);
                // Send confirmation back
                char response[256];
                int resp_size = protocol_create_message(response, sizeof(response), MSG_CLIENT_REGISTER, 
                                                       client_id, 0, NULL, 0);
                if (resp_size > 0) {
                    send(client_socket, response, resp_size, 0);
                }
                break;
            }
            
            case MSG_PING: {
                printf("Client %u sent ping\n", client_id);
                // Send pong back
                char response[256];
                int resp_size = protocol_create_message(response, sizeof(response), MSG_PONG, 
                                                       client_id, 0, NULL, 0);
                if (resp_size > 0) {
                    send(client_socket, response, resp_size, 0);
                }
                break;
            }
            
            case MSG_CLIENT_TIMEOUT: {
                printf("Client %u requested disconnect\n", client_id);
                keepalive = 0;
                break;
            }
            
            default: {
                printf("Client %u sent unknown message type: %u\n", client_id, header.message_type);
                break;
            }
        }
    }
    
    // Cleanup
    printf("Closing connection for client %u\n", client_id);
    close(client_socket);
    client_table_remove(clients, client_id);
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
    
    // Override with environment variables if set
    const char* env_port = getenv("BRIDGE_TCP_PORT");
    if (env_port) {
        listen_port = atoi(env_port);
        printf("Using port from environment: %d\n", listen_port);
    }
    
    const char* env_target_host = getenv("TARGET_UDP_HOST");
    const char* env_target_port = getenv("TARGET_UDP_PORT");
    if (env_target_host && env_target_port) {
        strncpy(target_host, env_target_host, sizeof(target_host) - 1);
        target_port = atoi(env_target_port);
        printf("Using target from environment: %s:%d\n", target_host, target_port);
    }
    
    const char* env_max_clients = getenv("MAX_CLIENTS");
    if (env_max_clients) {
        max_clients = atoi(env_max_clients);
        printf("Using max clients from environment: %u\n", max_clients);
    }
    
    const char* env_timeout = getenv("CLIENT_TIMEOUT");
    if (env_timeout) {
        client_timeout = atoi(env_timeout);
        printf("Using client timeout from environment: %ld\n", client_timeout);
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
    
    // Initialize UDP forwarder
    udp_forwarder_t* forwarder = udp_forwarder_create(target_host, target_port, global_client_table);
    if (!forwarder) {
        fprintf(stderr, "Failed to create UDP forwarder\n");
        close(server_socket);
        client_table_destroy(global_client_table);
        return 1;
    }
    
    // Start UDP forwarder
    if (udp_forwarder_start(forwarder) != 0) {
        fprintf(stderr, "Failed to start UDP forwarder\n");
        udp_forwarder_destroy(forwarder);
        close(server_socket);
        client_table_destroy(global_client_table);
        return 1;
    }
    
    printf("UDP forwarder started: %s:%d\n", target_host, target_port);
    
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
            if (++stats_counter >= 10) {  // Every 10 seconds
                uint32_t active_clients = client_table_get_count(global_client_table);
                printf("\n=== Server Statistics ===\n");
                printf("Active clients: %u\n", active_clients);
                
                // Print UDP forwarder stats
                udp_forwarder_print_stats(forwarder);
                printf("========================\n\n");
                
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
        
        // Handle client connection in a separate thread or with select
        // For now, implement basic protocol handling in main thread
        handle_client_connection(client_socket, client_id, forwarder, global_client_table);
    }
    
    printf("\nShutting down server...\n");
    
    // Cleanup
    close(server_socket);
    udp_forwarder_stop(forwarder);
    udp_forwarder_destroy(forwarder);
    client_table_destroy(global_client_table);
    
    printf("Server shutdown complete\n");
    return 0;
}
