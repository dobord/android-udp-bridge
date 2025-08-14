#include "udp2tcp_client_adapter.h"
#include <pthread.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/select.h>
#ifdef __ANDROID__
#include <android/log.h>
#endif

#ifndef LOG_TAG
#define LOG_TAG "Udp2TcpAdapter"
#endif

#ifdef __ANDROID__
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#else
#define LOGI(...) do { fprintf(stdout, "[I] " __VA_ARGS__); fprintf(stdout, "\n"); } while (0)
#define LOGE(...) do { fprintf(stderr, "[E] " __VA_ARGS__); fprintf(stderr, "\n"); } while (0)
#endif

static int g_local_udp_port = 0;
static char g_remote_host[256];
static int g_remote_port = 0;
static volatile int g_running = 0;
static pthread_t g_thread;

// Track last client (single-client optimization placeholder)
static struct sockaddr_in g_last_client_addr;
static int g_have_client = 0;

// Statistics
static uint64_t g_rx_packets = 0;
static uint64_t g_tx_packets = 0;
static uint64_t g_rx_bytes = 0;
static uint64_t g_tx_bytes = 0;

// Simple framing (placeholder) — в реальной интеграции заменить на формат udp2tcp
// Frame: [2 bytes length][payload]

static void* adapter_thread(void* arg) {
    (void)arg;
    LOGI("udp2tcp adapter thread started (stub implementation)");

    int udp_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (udp_sock < 0) {
        LOGE("Failed to create UDP socket: %s", strerror(errno));
        return NULL;
    }

    struct sockaddr_in laddr; memset(&laddr, 0, sizeof(laddr));
    laddr.sin_family = AF_INET;
    laddr.sin_addr.s_addr = htonl(INADDR_ANY);
    laddr.sin_port = htons(g_local_udp_port);
    if (bind(udp_sock, (struct sockaddr*)&laddr, sizeof(laddr)) < 0) {
        LOGE("Failed to bind UDP %d: %s", g_local_udp_port, strerror(errno));
        close(udp_sock);
        return NULL;
    }

    // TCP socket (SSH forward предполагается уже настроен: remote_host:remote_port локально доступен)
    int tcp_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (tcp_sock < 0) {
        LOGE("Failed to create TCP socket: %s", strerror(errno));
        close(udp_sock);
        return NULL;
    }
    struct sockaddr_in raddr; memset(&raddr, 0, sizeof(raddr));
    raddr.sin_family = AF_INET;
    raddr.sin_port = htons(g_remote_port);
    if (inet_pton(AF_INET, g_remote_host, &raddr.sin_addr) != 1) {
        LOGE("Invalid remote host %s", g_remote_host);
        close(udp_sock); close(tcp_sock);
        return NULL;
    }
    LOGI("Connecting TCP to %s:%d", g_remote_host, g_remote_port);
    if (connect(tcp_sock, (struct sockaddr*)&raddr, sizeof(raddr)) < 0) {
        LOGE("Failed to connect tcp: %s", strerror(errno));
        close(udp_sock); close(tcp_sock);
        return NULL;
    }

    // Main loop (very simplified placeholder bridging logic)
    g_running = 1;
    while (g_running) {
        fd_set rfds; FD_ZERO(&rfds);
        FD_SET(udp_sock, &rfds);
        FD_SET(tcp_sock, &rfds);
        int maxfd = udp_sock > tcp_sock ? udp_sock : tcp_sock;
        struct timeval tv = {1,0};
        int ready = select(maxfd+1, &rfds, NULL, NULL, &tv);
        if (ready < 0) {
            if (errno == EINTR) continue;
            LOGE("select error: %s", strerror(errno));
            break;
        }
        if (ready == 0) continue;

        if (FD_ISSET(udp_sock, &rfds)) {
            char buf[1500]; struct sockaddr_in caddr; socklen_t clen=sizeof(caddr);
            int n = recvfrom(udp_sock, buf, sizeof(buf), 0, (struct sockaddr*)&caddr, &clen);
            if (n > 0) {
                // Remember last client
                g_last_client_addr = caddr; g_have_client = 1;
                g_tx_packets++; g_tx_bytes += (uint64_t)n;
                // Frame and send to TCP
                unsigned char frame[2+1500];
                frame[0] = (unsigned char)((n >> 8) & 0xFF);
                frame[1] = (unsigned char)(n & 0xFF);
                memcpy(frame+2, buf, n);
                ssize_t w = send(tcp_sock, frame, n+2, 0);
                if (w < 0) LOGE("send tcp failed: %s", strerror(errno));
            }
        }
        if (FD_ISSET(tcp_sock, &rfds)) {
            unsigned char hdr[2];
            ssize_t r = recv(tcp_sock, hdr, 2, MSG_PEEK);
            if (r == 0) { LOGI("TCP closed by peer"); break; }
            if (r < 0) { LOGE("tcp recv err: %s", strerror(errno)); break; }
            if (r < 2) continue; // wait full header
            // read full header
            recv(tcp_sock, hdr, 2, 0);
            int len = ((int)hdr[0] << 8) | hdr[1];
            if (len <= 0 || len > 1500) { LOGE("invalid frame len %d", len); break; }
            char payload[1500];
            int off=0; while (off < len) {
                int chunk = recv(tcp_sock, payload+off, len-off, 0);
                if (chunk <=0) { LOGE("tcp payload err"); goto end; }
                off += chunk;
            }
            g_rx_packets++; g_rx_bytes += (uint64_t)len;
            if (g_have_client) {
                // Send back to last known client (single-client scenario)
                ssize_t s = sendto(udp_sock, payload, len, 0, (struct sockaddr*)&g_last_client_addr, sizeof(g_last_client_addr));
                if (s < 0) LOGE("sendto back failed: %s", strerror(errno));
            }
            // TODO: Multi-client mapping: нужен frame формат с client key (IP/port) или отдельная таблица
        }
    }
end:
    g_running = 0;
    close(udp_sock);
    // Attempt graceful tcp shutdown
    shutdown(tcp_sock, SHUT_RDWR);
    close(tcp_sock);
    LOGI("udp2tcp adapter thread exiting");
    return NULL;
}

int udp2tcp_init(const char* remote_host, int remote_port, int local_udp_port) {
    if (!remote_host) return -1;
    strncpy(g_remote_host, remote_host, sizeof(g_remote_host)-1);
    g_remote_host[sizeof(g_remote_host)-1] = '\0';
    g_remote_port = remote_port;
    g_local_udp_port = local_udp_port;
    return 0;
}

int udp2tcp_start(void) {
    if (g_running) return 0;
    if (pthread_create(&g_thread, NULL, adapter_thread, NULL) != 0) {
        LOGE("Failed to start adapter thread");
        return -1;
    }
    return 0;
}

int udp2tcp_stop(void) {
    if (!g_running) return 0;
    g_running = 0;
    return 0;
}

void udp2tcp_cleanup(void) {
    if (g_thread) {
        pthread_join(g_thread, NULL);
        g_thread = 0;
    }
}

void udp2tcp_get_stats(uint64_t* rx_packets, uint64_t* tx_packets, uint64_t* rx_bytes, uint64_t* tx_bytes) {
    if (rx_packets) *rx_packets = g_rx_packets;
    if (tx_packets) *tx_packets = g_tx_packets;
    if (rx_bytes) *rx_bytes = g_rx_bytes;
    if (tx_bytes) *tx_bytes = g_tx_bytes;
}

int udp2tcp_is_running(void) { return g_running; }
