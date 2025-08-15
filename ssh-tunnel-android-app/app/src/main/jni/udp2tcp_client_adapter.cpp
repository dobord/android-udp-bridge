#include "udp2tcp_client_adapter.h"
#include <atomic>
#include <string>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include "udp2tcp/c_api.h"

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

static int g_local_udp_port = 0;          // local UDP listen port
static std::string g_remote_host;         // udp2tcp server host (accessible via SSH forward)
static int g_remote_port = 0;             // udp2tcp server TCP port
static std::string g_dst_ip = "127.0.0.1"; // remote destination IP (default loopback on server)
static int g_dst_port = 0;                // remote destination port (default same as local_udp_port)
static std::atomic<int> g_running{0};     // running state flag
static udp2tcp_client* g_client_handle = nullptr; // C API client handle

// Simple statistics (not yet aggregated from internal library metrics)
static std::atomic<uint64_t> g_rx_packets{0};
static std::atomic<uint64_t> g_tx_packets{0};
static std::atomic<uint64_t> g_rx_bytes{0};
static std::atomic<uint64_t> g_tx_bytes{0};

// Logging callback from udp2tcp
static void udp2tcp_log_cb(int level, const char* message, void* /*user*/) {
    if(!message) return;
    switch(level) {
        case 0: LOGI("udp2tcp[debug]: %s", message); break;
        case 1: LOGI("udp2tcp[info]: %s", message); break;
        case 2: LOGE("udp2tcp[warn]: %s", message); break;
        case 3: LOGE("udp2tcp[error]: %s", message); break;
        default: LOGI("udp2tcp[%d]: %s", level, message); break;
    }
}

int udp2tcp_init(const char* remote_host, int remote_port, int local_udp_port)
{
    if (!remote_host) return -1;
    g_remote_host = remote_host;
    g_remote_port = remote_port;
    g_local_udp_port = local_udp_port;
    g_dst_ip = "127.0.0.1";
    g_dst_port = local_udp_port;
    return 0;
}

int udp2tcp_init_advanced(const char* remote_host, int remote_port, int local_udp_port,
                          const char* dst_ip, int dst_port)
{
    if (udp2tcp_init(remote_host, remote_port, local_udp_port) != 0) return -1;
    if (dst_ip && *dst_ip) g_dst_ip = dst_ip;
    if (dst_port > 0) g_dst_port = dst_port; else g_dst_port = local_udp_port;
    return 0;
}

int udp2tcp_start(void)
{
    if (g_running.load()) return 0; // already running
    udp2tcp_set_log_callback(udp2tcp_log_cb, nullptr);

    // Forward one local port to remote (placeholder: echo the same port). Can be extended via JNI.
    static udp2tcp_udp_forward_item fwd{}; // static lifetime
    fwd.name = "default";
    fwd.listen_addr = "0.0.0.0";
    fwd.listen_port = static_cast<uint16_t>(g_local_udp_port);
    fwd.remote_dst_ip = g_dst_ip.c_str();
    fwd.remote_dst_port = static_cast<uint16_t>(g_dst_port);
    fwd.recv_buffer_bytes = 0;
    fwd.send_buffer_bytes = 0;

    udp2tcp_client_cfg cfg{};
    cfg.tcp_connect.host = g_remote_host.c_str();
    cfg.tcp_connect.port = static_cast<uint16_t>(g_remote_port);
    cfg.tcp_connect.tls.enabled = 0;
    cfg.tcp_connect.tls.verify_peer = 0;
    cfg.tcp_connect.tls.certificate = nullptr;
    cfg.tcp_connect.tls.private_key = nullptr;
    cfg.auth.mode = "none";
    cfg.auth.token = "";
    cfg.udp_forward = &fwd;
    cfg.udp_forward_len = 1;
    cfg.limits.max_frame_bytes = 0;
    cfg.limits.max_inflight_frames = 0;
    cfg.logging.level = "info";
    cfg.logging.format = "text";
    cfg.metrics.enabled = 0;
    cfg.metrics.listen_addr = nullptr;
    cfg.metrics.listen_port = 0;

    int rc = udp2tcp_client_start(&cfg, &g_client_handle);
    if (rc != 0) {
        LOGE("udp2tcp_client_start failed rc=%d", rc);
        g_client_handle = nullptr;
        return -1;
    }
    g_running.store(1);
    return 0;
}

int udp2tcp_stop(void)
{
    if (!g_running.load()) return 0;
    if (g_client_handle) {
        udp2tcp_client_stop(g_client_handle);
    }
    return 0; // join is performed in cleanup
}

void udp2tcp_cleanup(void)
{
    if (g_client_handle) {
        int exit_code = 0;
        udp2tcp_client_join(g_client_handle, &exit_code);
        if (exit_code == -100) {
            LOGI("udp2tcp client joined: minimal embed mode (exit_code=-100, scheduler disabled)");
        } else {
            LOGI("udp2tcp client joined exit_code=%d", exit_code);
        }
        g_client_handle = nullptr;
    }
    g_running.store(0);
}

void udp2tcp_get_stats(uint64_t* rx_packets, uint64_t* tx_packets, uint64_t* rx_bytes, uint64_t* tx_bytes)
{
    if (rx_packets) *rx_packets = g_rx_packets.load();
    if (tx_packets) *tx_packets = g_tx_packets.load();
    if (rx_bytes) *rx_bytes = g_rx_bytes.load();
    if (tx_bytes) *tx_bytes = g_tx_bytes.load();
}

int udp2tcp_get_library_stats(uint64_t* tx_frames, uint64_t* rx_frames,
                              uint64_t* tx_bytes, uint64_t* rx_bytes)
{
    if (!g_client_handle) return -1;
    udp2tcp_client_stats st{};
    if (udp2tcp_client_get_stats(g_client_handle, &st) != 0) return -2;
    if (tx_frames) *tx_frames = st.tx_frames;
    if (rx_frames) *rx_frames = st.rx_frames;
    if (tx_bytes) *tx_bytes = st.tx_bytes;
    if (rx_bytes) *rx_bytes = st.rx_bytes;
    return 0;
}

int udp2tcp_is_running(void) { return g_running.load(); }
