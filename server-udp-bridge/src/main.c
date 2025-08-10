#include "client_table.h"
#include "protocol.h"
#include "udp_forwarder.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>

// Global variables for graceful shutdown
static volatile int running = 1;
static client_table_t* global_client_table = NULL;
static int shutdown_pipe[2] = {-1, -1};

void signal_handler(int sig) {
    (void)sig;
    printf("\nReceived shutdown signal, cleaning up...\n");
    running = 0;
    
    // Wake up select() by writing to shutdown pipe
    if (shutdown_pipe[1] != -1) {
        char dummy = 1;
        write(shutdown_pipe[1], &dummy, 1);
    }
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
void handle_protocol_message(int client_socket, uint32_t client_id, 
                            const char* buffer, ssize_t received, udp_forwarder_t* forwarder);

// Helper function to recalculate max_fd from fd_set
int recalc_max_fd(fd_set* fds, int server_socket, int shutdown_pipe_fd, int current_max) {
    int new_max = server_socket;
    if (shutdown_pipe_fd > new_max) new_max = shutdown_pipe_fd;
    
    for (int fd = 0; fd <= current_max; fd++) {
        if (FD_ISSET(fd, fds) && fd > new_max) {
            new_max = fd;
        }
    }
    return new_max;
}

// Protocol message handler for select() based architecture
void handle_protocol_message(int client_socket, uint32_t client_id, 
                            const char* buffer, ssize_t received, udp_forwarder_t* forwarder) {
    // Parse protocol message
    udp_bridge_header_t header;
    if (protocol_parse_header(buffer, received, &header) != 0) {
        printf("Client %u sent invalid protocol message\n", client_id);
        return;
    }
    
    // Verify client ID matches
    if (header.client_id != client_id) {
        printf("Client %u sent message with wrong client_id %u\n", client_id, header.client_id);
        return;
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
                send(client_socket, response, resp_size, MSG_DONTWAIT);
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
                send(client_socket, response, resp_size, MSG_DONTWAIT);
            }
            break;
        }
        
        case MSG_CLIENT_TIMEOUT: {
            printf("Client %u requested disconnect\n", client_id);
            // Client will be removed by the main loop when socket closes
            break;
        }
        
        default: {
            printf("Client %u sent unknown message type: %u\n", client_id, header.message_type);
            break;
        }
    }
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
    
    // Create shutdown pipe for signal handling
    if (pipe(shutdown_pipe) == -1) {
        perror("Failed to create shutdown pipe");
        return 1;
    }
    
    // Set shutdown pipe read end to non-blocking
    int flags = fcntl(shutdown_pipe[0], F_GETFL, 0);
    fcntl(shutdown_pipe[0], F_SETFL, flags | O_NONBLOCK);
    
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
    
    // Main server loop with select() for multiple clients
    fd_set master_fds, read_fds;
    int max_fd = server_socket;
    
    FD_ZERO(&master_fds);
    FD_SET(server_socket, &master_fds);
    FD_SET(shutdown_pipe[0], &master_fds);
    
    if (shutdown_pipe[0] > max_fd) {
        max_fd = shutdown_pipe[0];
    }
    
    printf("Server ready to handle multiple clients with select()\n\n");
    
    while (running) {
        // Copy master fd set
        read_fds = master_fds;
        
        struct timeval timeout;
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;
        
        int activity = select(max_fd + 1, &read_fds, NULL, NULL, &timeout);
        
        if (activity < 0) {
            if (errno == EINTR) {
                printf("Select interrupted by signal\n");
                continue;
            } else {
                perror("Select error");
                break;
            }
        }
        
        // Check for shutdown signal
        if (FD_ISSET(shutdown_pipe[0], &read_fds)) {
            char dummy;
            while (read(shutdown_pipe[0], &dummy, 1) > 0); // drain pipe
            printf("Shutdown signal received via pipe\n");
            break;
        }
        
        if (activity == 0) {
            // Timeout - print stats every few seconds
            static int stats_counter = 0;
            if (++stats_counter >= 10) {  // Every 10 seconds
                uint32_t active_clients = client_table_get_count(global_client_table);
                printf("\n=== Server Statistics ===\n");
                printf("Active clients: %u\n", active_clients);
                printf("Max FD: %d\n", max_fd);
                
                // Print UDP forwarder stats
                udp_forwarder_print_stats(forwarder);
                printf("========================\n\n");
                
                stats_counter = 0;
            }
            continue;
        }
        
        // Check for new connections on server socket
        if (FD_ISSET(server_socket, &read_fds)) {
            struct sockaddr_in client_addr;
            socklen_t client_len = sizeof(client_addr);
            
            int client_socket = accept(server_socket, (struct sockaddr*)&client_addr, &client_len);
            if (client_socket < 0) {
                if (errno != EAGAIN && errno != EWOULDBLOCK) {
                    perror("Failed to accept connection");
                }
            } else {
                printf("New connection from %s:%d\n", 
                       inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
                
                // Add client to table
                uint32_t client_id = client_table_add(global_client_table, client_socket);
                if (client_id == 0) {
                    fprintf(stderr, "Failed to add client (table full?)\n");
                    close(client_socket);
                } else {
                    printf("Assigned client ID: %u\n", client_id);
                    
                    // Set client socket to non-blocking mode
                    int flags = fcntl(client_socket, F_GETFL, 0);
                    fcntl(client_socket, F_SETFL, flags | O_NONBLOCK);
                    
                    // Add to master fd set
                    FD_SET(client_socket, &master_fds);
                    if (client_socket > max_fd) {
                        max_fd = client_socket;
                    }
                    
                    printf("Client %u ready for non-blocking I/O\n", client_id);
                }
            }
        }
        
        // Check for data from existing clients
        // Use a list of active fds to avoid iterating over all possible fd values
        int active_fds[FD_SETSIZE];
        int active_count = 0;
        
        // Collect active file descriptors (excluding server socket and shutdown pipe)
        for (int fd = 0; fd <= max_fd && active_count < FD_SETSIZE; fd++) {
            if (fd != server_socket && fd != shutdown_pipe[0] && FD_ISSET(fd, &read_fds)) {
                active_fds[active_count++] = fd;
            }
        }
        
        // Process active client connections
        for (int i = 0; i < active_count; i++) {
            int fd = active_fds[i];
            
            // Find client by socket
            client_entry_t* client = client_table_find_by_socket(global_client_table, fd);
            if (!client) {
                // Client not found, remove from fd set and close
                printf("Closing connection for unknown socket %d\n", fd);
                FD_CLR(fd, &master_fds);
                close(fd);
                // Recalculate max_fd after removing fd
                max_fd = recalc_max_fd(&master_fds, server_socket, shutdown_pipe[0], max_fd);
                continue;
            }
            
            // Handle client data
            char buffer[4096];
            ssize_t received = recv(fd, buffer, sizeof(buffer), 0);
            
            if (received <= 0) {
                if (received == 0) {
                    printf("Client %u disconnected\n", client->client_id);
                } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
                    printf("Client %u receive error: %s\n", client->client_id, strerror(errno));
                } else {
                    // EAGAIN/EWOULDBLOCK - no data available, continue
                    continue;
                }
                
                // Remove client cleanly
                printf("Closing connection for client %u\n", client->client_id);
                FD_CLR(fd, &master_fds);
                close(fd);
                client_table_remove(global_client_table, client->client_id);
                // Recalculate max_fd after removing fd
                max_fd = recalc_max_fd(&master_fds, server_socket, shutdown_pipe[0], max_fd);
                continue;
            }
            
            // Update client activity
            client_table_update_activity(global_client_table, client->client_id);
            
            // Handle incoming data in protocol message handler
            printf("Handling client %u connection\n", client->client_id);
            handle_protocol_message(fd, client->client_id, buffer, received, forwarder);
        }
    }
    
    printf("\nShutting down server...\n");
    
    // First, close all client connections
    for (int fd = 0; fd <= max_fd; fd++) {
        if (fd != server_socket && fd != shutdown_pipe[0] && fd != shutdown_pipe[1] && FD_ISSET(fd, &master_fds)) {
            client_entry_t* client = client_table_find_by_socket(global_client_table, fd);
            if (client) {
                printf("Closing client %u connection (socket %d)\n", client->client_id, fd);
            }
            close(fd);
        }
    }
    
    // Cleanup in proper order
    printf("Stopping UDP forwarder...\n");
    udp_forwarder_stop(forwarder);
    
    printf("Destroying UDP forwarder...\n");
    udp_forwarder_destroy(forwarder);
    
    printf("Destroying client table...\n");
    client_table_destroy(global_client_table);
    
    // Close server socket
    printf("Closing server socket...\n");
    close(server_socket);
    
    // Close shutdown pipe
    if (shutdown_pipe[0] != -1) {
        close(shutdown_pipe[0]);
        shutdown_pipe[0] = -1;
    }
    if (shutdown_pipe[1] != -1) {
        close(shutdown_pipe[1]);
        shutdown_pipe[1] = -1;
    }
    
    printf("Server shutdown complete\n");
    return 0;
}
