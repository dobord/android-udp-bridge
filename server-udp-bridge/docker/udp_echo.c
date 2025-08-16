#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    const char *bind_ip = "127.0.0.1";
    int port = 9001;
    if (argc >= 2)
        bind_ip = argv[1];
    if (argc >= 3)
        port = atoi(argv[2]);
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        perror("socket");
        return 1;
    }
    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons((uint16_t)port);
    if (inet_pton(AF_INET, bind_ip, &sa.sin_addr) != 1) {
        perror("inet_pton");
        return 2;
    }
    int on = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
    if (bind(fd, (struct sockaddr *)&sa, sizeof(sa)) != 0) {
        perror("bind");
        return 3;
    }
    char buf[65536];
    while (1) {
        struct sockaddr_in from;
        socklen_t flen = sizeof(from);
        ssize_t n = recvfrom(fd, buf, sizeof(buf), 0, (struct sockaddr *)&from, &flen);
        if (n <= 0)
            continue;
        sendto(fd, buf, (size_t)n, 0, (struct sockaddr *)&from, flen);
    }
}
