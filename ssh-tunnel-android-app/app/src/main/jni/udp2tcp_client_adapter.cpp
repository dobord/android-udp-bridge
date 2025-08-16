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

static std::atomic<int> g_running{0};     // running state flag
static udp2tcp_client* g_client_handle = nullptr; // C API client handle
static std::string g_log_level = "info"; // default log level

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

// Provide C linkage for functions used by C file ssh_tunnel.c
extern "C" {

void udp2tcp_set_log_level(const char* level)
{
    if (level && *level) {
        g_log_level = level;
    }
}

int udp2tcp_start(const char* remoteBridgeHost,
                  int remoteBridgePort,
                  const char* localBridgeHost,
                  int localBridgePort,
                  const char* localUdpHost,
                  int localUdpPort,
                  const char* remoteUdpHost,
                  int remoteUdpPort)
{
    if (g_running.load()) return 0; // already running
    (void)remoteBridgeHost; (void)remoteBridgePort; // not used directly by adapter; logged upstream
    if (!localBridgeHost || !localUdpHost || !remoteUdpHost) return -1;
    if (localBridgePort <= 0 || localUdpPort <= 0 || remoteUdpPort <= 0) return -1;

    udp2tcp_set_log_callback(udp2tcp_log_cb, nullptr);

    // Forward one local port to remote with provided parameters
    static udp2tcp_udp_forward_item fwd{}; // static lifetime is OK while running
    fwd.name = "default";
    fwd.listen_addr = localUdpHost;
    fwd.listen_port = static_cast<uint16_t>(localUdpPort);
    fwd.remote_dst_ip = remoteUdpHost;
    fwd.remote_dst_port = static_cast<uint16_t>(remoteUdpPort);
    fwd.recv_buffer_bytes = 0;
    fwd.send_buffer_bytes = 0;

    udp2tcp_client_cfg cfg{};
    cfg.tcp_connect.host = localBridgeHost;
    cfg.tcp_connect.port = static_cast<uint16_t>(localBridgePort);
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
    cfg.logging.level = g_log_level.c_str();
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
        udp2tcp_client_stop(g_client_handle); // triggers joinable state
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

} // extern "C"
