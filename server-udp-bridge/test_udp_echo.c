#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

// Simple UDP echo server for testing UDP bridge forwarding

int main(int argc, char *argv[])
{
    int port = 5060; // Default port

    if (argc > 1) {
        port = atoi(argv[1]);
        if (port <= 0 || port > 65535) {
            fprintf(stderr, "Invalid port: %s\n", argv[1]);
            return 1;
        }
    }

    printf("Starting UDP echo server on port %d...\n", port);

    // Create socket
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror("socket");
        return 1;
    }

    // Bind to port
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    if (bind(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind");
        close(sock);
        return 1;
    }

    printf("UDP echo server listening on 0.0.0.0:%d\n", port);
    printf("Press Ctrl+C to stop\n\n");

    char buffer[4096];
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);

    while (1) {
        // Receive data
        ssize_t received = recvfrom(sock, buffer, sizeof(buffer) - 1, 0, (struct sockaddr *)&client_addr, &client_len);

        if (received < 0) {
            perror("recvfrom");
            continue;
        }

        buffer[received] = '\0'; // Null terminate for printing

        printf(
            "Received %zd bytes from %s:%d: %s\n",
            received,
            inet_ntoa(client_addr.sin_addr),
            ntohs(client_addr.sin_port),
            buffer);

        // Echo back the data
        ssize_t sent = sendto(sock, buffer, received, 0, (struct sockaddr *)&client_addr, client_len);

        if (sent < 0) {
            perror("sendto");
        } else {
            printf("Echoed %zd bytes back\n", sent);
        }

        printf("---\n");
    }

    close(sock);
    return 0;
}
