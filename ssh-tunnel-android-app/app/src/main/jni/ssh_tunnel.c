#include <jni.h>
#ifdef USE_LIBSSH_MOCK
#include "libssh_mock.h"
#else
#include <libssh/libssh.h>
#include <libssh/callbacks.h>
#endif
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <android/log.h>
#include <signal.h>
#include <errno.h>
#include <stdint.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/select.h>
#include <time.h>

// udp2tcp integration only (legacy bridge removed)
#include "udp2tcp_client_adapter.h"

// Crypto library includes (OpenSSL only; mbedTLS support removed)
#ifndef USE_LIBSSH_MOCK
#ifdef USE_OPENSSL
#include <openssl/rand.h>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#endif
#endif

#ifndef LOG_TAG
#define LOG_TAG "SSHTunnelApp"
#endif
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)


static ssh_session session = NULL;
static int tunnel_active = 0;
static pthread_mutex_t session_mutex = PTHREAD_MUTEX_INITIALIZER;

// UDP Bridge listener integration
// Legacy only
// Legacy UDP bridge data structures removed

#ifndef USE_LIBSSH_MOCK
static volatile int g_libssh_initialized = 0;

#ifdef USE_OPENSSL
// Global OpenSSL state flag
static volatile int g_openssl_initialized = 0;
#endif

// Global libssh log callback (file-scope). Needed to compile with Clang (no nested functions).
static void ssh_android_log_cb(int priority, const char *function, const char *buffer, void *userdata) {
    (void)userdata;
    // Use INFO level to ensure messages are visible in logcat
    __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "libssh[%d] %s: %s", priority,
                        function ? function : "", buffer ? buffer : "");
}

// Android entropy source using /dev/urandom
static int android_entropy_source(void *data, unsigned char *output, size_t len, size_t *olen) {
    (void)data;
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd < 0) {
        LOGE("Failed to open /dev/urandom: %s", strerror(errno));
        return -1;
    }
    
    ssize_t bytes_read = read(fd, output, len);
    close(fd);
    
    if (bytes_read < 0) {
        LOGE("Failed to read from /dev/urandom: %s", strerror(errno));
        return -1;
    }
    
    *olen = (size_t)bytes_read;
    LOGI("Generated %zu bytes of entropy from /dev/urandom", *olen);
    return 0;
}

#ifdef USE_OPENSSL
// Direct OpenSSL initialization as workaround for libssh init failure
static int init_openssl_directly() {
    if (g_openssl_initialized) {
        LOGI("OpenSSL already initialized");
        return 0;
    }
    
    LOGI("Initializing OpenSSL directly");
    
    // Initialize OpenSSL
    SSL_library_init();
    SSL_load_error_strings();
    OpenSSL_add_all_algorithms();
    
    // Seed the random number generator
    unsigned char entropy_buf[32];
    size_t entropy_len;
    if (android_entropy_source(NULL, entropy_buf, sizeof(entropy_buf), &entropy_len) == 0) {
        RAND_seed(entropy_buf, (int)entropy_len);
        LOGI("OpenSSL seeded with %zu bytes of entropy", entropy_len);
    } else {
        LOGE("Failed to seed OpenSSL with entropy");
        return -1;
    }
    
    g_openssl_initialized = 1;
    LOGI("OpenSSL initialized successfully");
    return 0;
}
#endif

#ifdef USE_OPENSSL
static int force_crypto_init() { return init_openssl_directly(); }
#else
static int force_crypto_init() { return 0; }
#endif

// Enhanced initialization with entropy source
static int init_libssh_with_entropy() {
    LOGI("Attempting to initialize libssh with custom entropy source");
    
    // Force crypto initialization first
    if (force_crypto_init() != 0) {
        LOGE("Failed to force crypto initialization");
        return -1;
    }
    
    // First try to initialize entropy directly through /dev/urandom
    unsigned char entropy_buf[32];
    size_t entropy_len;
    
    if (android_entropy_source(NULL, entropy_buf, sizeof(entropy_buf), &entropy_len) == 0) {
        LOGI("Successfully generated %zu bytes of entropy", entropy_len);
    } else {
        LOGW("Failed to generate entropy, proceeding without custom source");
    }
    
    // Setup threading before any libssh calls
    // Note: libssh should handle threading internally
    
    // Attempt standard ssh_init
    int ret = ssh_init();
    if (ret == SSH_OK) {
        LOGI("ssh_init succeeded");
        return 1; // Success
    } else {
        LOGW("ssh_init failed with code %d, proceeding with manual crypto", ret);
        return -1; // Failed but will continue with manual crypto
    }
}
#endif

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* reserved) {
    (void)vm; (void)reserved;  // Suppress unused parameter warnings
    // Avoid process kill on SIGPIPE when writing to a closed socket/channel
    signal(SIGPIPE, SIG_IGN);
    
#ifndef USE_LIBSSH_MOCK
    LOGI("JNI_OnLoad: Starting libssh initialization");
    
    // Setup libssh logging for better diagnostics
    LOGI("JNI_OnLoad: Setting up libssh logging");
    ssh_set_log_callback(ssh_android_log_cb);
    // Max verbosity to trace all operations and crypto details
    ssh_set_log_level(SSH_LOG_TRACE);
    LOGI("JNI_OnLoad: libssh logging configured");
    
    // Initialize libssh (global crypto/RNG)
    LOGI("JNI_OnLoad: Attempting enhanced initialization with entropy");
    
    // Test direct crypto functionality (version + session allocation)
    LOGI("JNI_OnLoad: Testing libssh version info");
    const char* version = ssh_version(0);
    if (version) {
        LOGI("JNI_OnLoad: libssh version: %s", version);
    } else {
        LOGE("JNI_OnLoad: Failed to get libssh version");
    }
    
    // Check if we can create a session without init
    LOGI("JNI_OnLoad: Testing ssh_new() without init");
    ssh_session test_session = ssh_new();
    if (test_session) {
        LOGI("JNI_OnLoad: ssh_new() succeeded without init");
        ssh_free(test_session);
    } else {
        LOGE("JNI_OnLoad: ssh_new() failed without init");
    }
    
    // Try enhanced initialization with entropy
    int init_result = init_libssh_with_entropy();
    g_libssh_initialized = init_result;
    
    if (init_result == 1) {
        LOGI("JNI_OnLoad: libssh initialized successfully with entropy");
    } else {
        LOGI("JNI_OnLoad: Proceeding without global ssh_init - will try per-session initialization");
    }
#else
    LOGI("JNI_OnLoad: Mock mode - skipping libssh initialization");
#endif
    
    return JNI_VERSION_1_6;
}

#ifndef USE_LIBSSH_MOCK
JNIEXPORT void JNICALL JNI_OnUnload(JavaVM* vm, void* reserved) {
    (void)vm; (void)reserved;  // Suppress unused parameter warnings
    // Finalize libssh on library unload
    if (g_libssh_initialized) {
        ssh_finalize();
        g_libssh_initialized = 0;
    }
    
#ifdef USE_OPENSSL
    if (g_openssl_initialized) {
        EVP_cleanup();
        ERR_free_strings();
        g_openssl_initialized = 0;
    }
#endif // USE_OPENSSL
    
    LOGI("JNI_OnUnload: Cleanup completed");
}
#else
JNIEXPORT void JNICALL JNI_OnUnload(JavaVM* vm, void* reserved) {
    (void)vm; (void)reserved;  // Suppress unused parameter warnings
    LOGI("JNI_OnUnload: Mock mode - no cleanup needed");
}
#endif

// Port forward configuration passed to thread at start (no globals)
struct pf_config {
    char remote_host[128]; // SSH remote host for direct-tcpip (typically 127.0.0.1 on server)
    int  remote_port;      // server-side TCP destination port
    int  listen_port;      // local 127.0.0.1:<port> we expose
};

// Global variable to track port forwarding thread
static pthread_t port_forward_thread = 0;
static volatile int port_forward_running = 0; // volatile to ensure visibility across threads
static int g_listen_sock = -1; // listening socket to allow external close during shutdown

// Port forward thread state machine for debugging disconnect hangs
enum port_forward_state_e {
    PF_STATE_NONE = 0,
    PF_STATE_STARTING,
    PF_STATE_LISTEN_READY,
    PF_STATE_WAITING_ACCEPT,
    PF_STATE_CLIENT_ACCEPTED,
    PF_STATE_FORWARD_LOOP,
    PF_STATE_SHUTTING_DOWN,
    PF_STATE_EXIT
};
static volatile enum port_forward_state_e g_pf_state = PF_STATE_NONE;
static volatile uint64_t g_pf_last_state_change_mono_ns = 0;

static uint64_t monotonic_ns() {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static const char* pf_state_str(enum port_forward_state_e st) {
    switch (st) {
        case PF_STATE_NONE: return "NONE";
        case PF_STATE_STARTING: return "STARTING";
        case PF_STATE_LISTEN_READY: return "LISTEN_READY";
        case PF_STATE_WAITING_ACCEPT: return "WAITING_ACCEPT";
        case PF_STATE_CLIENT_ACCEPTED: return "CLIENT_ACCEPTED";
        case PF_STATE_FORWARD_LOOP: return "FORWARD_LOOP";
        case PF_STATE_SHUTTING_DOWN: return "SHUTTING_DOWN";
        case PF_STATE_EXIT: return "EXIT";
        default: return "?";
    }
}

static void pf_set_state(enum port_forward_state_e st) {
    g_pf_state = st;
    g_pf_last_state_change_mono_ns = monotonic_ns();
    LOGI("PortForwardState -> %s", pf_state_str(st));
}

// Forward buffer (simple dynamic heap buffer for partial writes)
struct forward_buffer { char *data; size_t size; size_t off; };

static void fb_init(struct forward_buffer *fb) { fb->data = NULL; fb->size = fb->off = 0; }

static void fb_dispose(struct forward_buffer *fb) {
    if (fb->data) free(fb->data);
    fb->data = NULL; fb->size = fb->off = 0;
}

// Append data, performing compaction; returns 0 on success, -1 on OOM/overflow
static int fb_append(struct forward_buffer *fb, const char *src, size_t len, size_t max_cap) {
    if (len == 0) return 0;
    size_t pending = fb->size - fb->off;
    if (pending + len > max_cap) return -1;
    if (fb->off > 0) { // compact
        if (pending > 0) memmove(fb->data, fb->data + fb->off, pending);
        fb->size = pending;
        fb->off = 0;
    }
    char *nd = (char*)realloc(fb->data, fb->size + len);
    if (!nd) return -1;
    fb->data = nd;
    memcpy(fb->data + fb->size, src, len);
    fb->size += len;
    return 0;
}

// Flush buffer to SSH channel (non-blocking); connection_active becomes 0 on fatal error
static void fb_flush_to_ssh(struct forward_buffer *fb, ssh_channel channel, int *connection_active, volatile int *running) {
    while (*running && *connection_active && fb->off < fb->size) {
        int w = ssh_channel_write(channel, fb->data + fb->off, (uint32_t)(fb->size - fb->off));
        if (w == SSH_AGAIN) {
            break; // need wait
        } else if (w == SSH_ERROR || w == SSH_EOF) {
            LOGE("flush_to_ssh: write error (%d)", w);
            *connection_active = 0;
            break;
        } else if (w > 0) {
            fb->off += (size_t)w;
        } else { // w == 0 unexpected
            break;
        }
    }
    if (fb->off == fb->size) {
        fb->off = fb->size = 0;
    }
}

// Flush buffer to client socket; connection_active becomes 0 on fatal error/close
static void fb_flush_to_client(struct forward_buffer *fb, int client_sock, int *connection_active, volatile int *running) {
    while (*running && *connection_active && fb->off < fb->size) {
        ssize_t s = send(client_sock, fb->data + fb->off, fb->size - fb->off, 0);
        if (s < 0) {
            if (errno == EWOULDBLOCK || errno == EAGAIN) break;
            LOGE("flush_to_client: send error: %s", strerror(errno));
            *connection_active = 0;
            break;
        } else if (s == 0) {
            *connection_active = 0; // peer closed
            break;
        } else {
            fb->off += (size_t)s;
        }
    }
    if (fb->off == fb->size) {
        fb->off = fb->size = 0;
    }
}

// TCP Port forwarding thread function
void* tcp_port_forward_thread(void* arg) {
    struct pf_config* cfg = (struct pf_config*)arg;
    
    LOGI("TCP port forwarding thread started");
    pf_set_state(PF_STATE_STARTING);
    
    int listen_port = cfg ? cfg->listen_port : 8080;
    int remote_port = cfg ? cfg->remote_port : 8080;
    const char* remote_host = (cfg && cfg->remote_host[0] != '\0') ? cfg->remote_host : "127.0.0.1";
    // Free cfg early; values are copied above
    if (cfg) { free(cfg); cfg = NULL; }

    LOGI("Port forward configuration: local=%d -> remote %s:%d", listen_port, remote_host, remote_port);

    // Create a local socket to listen
    int listen_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_sock < 0) {
        LOGE("Failed to create listening socket for port forwarding");
        return NULL;
    }
    g_listen_sock = listen_sock;
    
    // Enable socket reuse
    int opt = 1;
    setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    struct sockaddr_in listen_addr;
    memset(&listen_addr, 0, sizeof(listen_addr));
    listen_addr.sin_family = AF_INET;
    listen_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    listen_addr.sin_port = htons(listen_port);
    
    if (bind(listen_sock, (struct sockaddr*)&listen_addr, sizeof(listen_addr)) < 0) {
        LOGE("Failed to bind to port %d for forwarding: %s", listen_port, strerror(errno));
        close(listen_sock);
        return NULL;
    }
    
    if (listen(listen_sock, 5) < 0) {
        LOGE("Failed to listen on port %d: %s", listen_port, strerror(errno));
        close(listen_sock);
        return NULL;
    }
    
    LOGI("TCP port forwarding listening on 127.0.0.1:%d", listen_port);
    pf_set_state(PF_STATE_LISTEN_READY);
    port_forward_running = 1;
    
    while (port_forward_running) {
        pf_set_state(PF_STATE_WAITING_ACCEPT);
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        
        int client_sock = accept(listen_sock, (struct sockaddr*)&client_addr, &client_len);
        if (client_sock < 0) {
            if (port_forward_running) {
                LOGE("Failed to accept connection: %s", strerror(errno));
            }
            continue;
        }
        
        LOGI("Accepted TCP connection for forwarding to remote %s:%d", remote_host, remote_port);
        pf_set_state(PF_STATE_CLIENT_ACCEPTED);
        
        // Create SSH channel for forwarding
        pthread_mutex_lock(&session_mutex);
        if (session != NULL) {
            ssh_channel channel = ssh_channel_new(session);
            if (channel != NULL) {
                int rc = SSH_ERROR;
                const int MAX_OPEN_ATTEMPTS = 10;
                const int RETRY_SLEEP_MS = 200;
                for (int attempt = 1; attempt <= MAX_OPEN_ATTEMPTS; ++attempt) {
                    rc = ssh_channel_open_forward(channel, remote_host, remote_port, "127.0.0.1", listen_port);
                    if (rc == SSH_OK) {
                        if (attempt > 1) {
                            LOGI("SSH forward channel opened after %d attempts", attempt);
                        }
                        break;
                    }
                    const char *err = ssh_get_error(session);
                    if (err) {
                        LOGW("open_forward attempt %d/%d failed: %s", attempt, MAX_OPEN_ATTEMPTS, err);
                        if (strstr(err, "Connection refused") != NULL || strstr(err, "connection refused") != NULL) {
                            if (attempt < MAX_OPEN_ATTEMPTS) {
                                struct timespec ts; ts.tv_sec = RETRY_SLEEP_MS / 1000; ts.tv_nsec = (RETRY_SLEEP_MS % 1000) * 1000000L; nanosleep(&ts, NULL);
                                continue; // retry
                            }
                        }
                    } else {
                        LOGW("open_forward attempt %d/%d failed (no error string)", attempt, MAX_OPEN_ATTEMPTS);
                    }
                    // For non-refused errors or last attempt, break
                    if (attempt == MAX_OPEN_ATTEMPTS) break;
                }
                if (rc == SSH_OK) {
                    LOGI("SSH channel opened for TCP forwarding %d -> %s:%d", listen_port, remote_host, remote_port);
                    
                    // Improved forwarding loop with non-blocking SSH channel & backpressure handling
                    ssh_channel_set_blocking(channel, 0);
                    char buffer[4096];
                    fd_set read_fds;
                    int session_fd = ssh_get_fd(session);
                    if (session_fd < 0) {
                        LOGW("ssh_get_fd returned <0; forwarding may not progress correctly");
                    }
                    int max_fd = (client_sock > session_fd) ? client_sock : session_fd;
                    uint64_t last_tick_sec = 0;
                    int connection_active = 1;
                    // Make client socket non-blocking (so send won't stall the loop)
                    int flags = fcntl(client_sock, F_GETFL, 0);
                    if (flags >= 0) fcntl(client_sock, F_SETFL, flags | O_NONBLOCK);

                    // Dynamic buffering for partial writes in both directions
                    struct forward_buffer to_ssh;     // client -> ssh
                    struct forward_buffer to_client;  // ssh -> client
                    fb_init(&to_ssh);
                    fb_init(&to_client);
                    const size_t MAX_BUFFER_CAP = 256*1024;  // 256 KiB cap per direction
                    
                    while (port_forward_running && connection_active) {
                        if (g_pf_state != PF_STATE_FORWARD_LOOP) pf_set_state(PF_STATE_FORWARD_LOOP);
                        FD_ZERO(&read_fds);
                        FD_SET(client_sock, &read_fds);
                        if (session_fd >= 0) {
                            FD_SET(session_fd, &read_fds);
                        }
                        struct timeval tv = {1, 0}; // 1 second tick granularity
                        int ready = select(max_fd + 1, &read_fds, NULL, NULL, &tv);
                        if (ready < 0) {
                            if (errno == EINTR) continue;
                            LOGE("select() error in forwarding loop: %s", strerror(errno));
                            break;
                        }
                        // Periodic tick to allow libssh internal housekeeping (rekey, keepalive, window adjust)
                        time_t now_sec = time(NULL);
                        if (now_sec != (time_t)last_tick_sec) {
                            last_tick_sec = (uint64_t)now_sec;
                            // Session tick: rely on nonblocking reads to process packets
                            if (session != NULL && !ssh_is_connected(session)) {
                                LOGW("SSH session no longer connected");
                                break;
                            }
                            // Try flushing pending both directions on tick
                            if (to_ssh.size > to_ssh.off) fb_flush_to_ssh(&to_ssh, channel, &connection_active, &port_forward_running);
                            if (to_client.size > to_client.off) fb_flush_to_client(&to_client, client_sock, &connection_active, &port_forward_running);
                        }
                        // Local client socket readable -> read and forward to SSH channel
                        if (FD_ISSET(client_sock, &read_fds)) {
                            for (;;) { // drain local socket
                                int bytes = recv(client_sock, buffer, sizeof(buffer), 0);
                                if (bytes == 0) { // client closed
                                    connection_active = 0;
                                    break;
                                } else if (bytes < 0) {
                                    if (errno == EWOULDBLOCK || errno == EAGAIN) {
                                        break; // no more data now
                                    }
                                    LOGE("recv() error: %s", strerror(errno));
                                    connection_active = 0;
                                    break;
                                }
                                // Append to to_ssh buffer (may flush immediately)
                                if (fb_append(&to_ssh, buffer, (size_t)bytes, MAX_BUFFER_CAP) < 0) {
                                    LOGE("to_ssh buffer overflow, closing");
                                    connection_active = 0;
                                    break;
                                }
                                fb_flush_to_ssh(&to_ssh, channel, &connection_active, &port_forward_running);
                                // If still pending and buffer large, pause further reads to apply backpressure
                                if (to_ssh.size - to_ssh.off > 64*1024) {
                                    break; // exit inner drain loop
                                }
                                continue; // attempt to read more from client
                            }
                        }
                        // SSH session fd readable -> process incoming packets
                        if (session_fd >= 0 && FD_ISSET(session_fd, &read_fds)) {
                            // SSH session socket readable; channel reads below will parse incoming packets
                            if (to_ssh.size > to_ssh.off) fb_flush_to_ssh(&to_ssh, channel, &connection_active, &port_forward_running);
                        }
                        // Drain SSH channel data to local client
                        while (port_forward_running && connection_active) {
                            int ssh_bytes = ssh_channel_read_nonblocking(channel, buffer, sizeof(buffer), 0);
                            if (ssh_bytes > 0) {
                                if (fb_append(&to_client, buffer, (size_t)ssh_bytes, MAX_BUFFER_CAP) < 0) {
                                    LOGE("to_client buffer overflow, closing");
                                    connection_active = 0;
                                    break;
                                }
                                fb_flush_to_client(&to_client, client_sock, &connection_active, &port_forward_running);
                                continue; // try to read even more from channel
                            } else if (ssh_bytes == 0 || ssh_bytes == SSH_AGAIN) {
                                break; // nothing more now
                            } else if (ssh_bytes == SSH_EOF || ssh_channel_is_eof(channel)) {
                                LOGI("SSH channel EOF reached");
                                connection_active = 0;
                                break;
                            } else if (ssh_bytes == SSH_ERROR) {
                                LOGE("ssh_channel_read_nonblocking error (SSH_ERROR)");
                                connection_active = 0;
                                break;
                            }
                        }
                        // Flush any pending to_client data (if channel produced earlier but socket was blocked)
                        if (to_client.size > to_client.off) fb_flush_to_client(&to_client, client_sock, &connection_active, &port_forward_running);
                        if (!ssh_channel_is_open(channel) || ssh_channel_is_closed(channel)) {
                            LOGI("SSH channel closed by remote side");
                            break;
                        }
                    }

                    fb_dispose(&to_ssh);
                    fb_dispose(&to_client);
                    
                    ssh_channel_close(channel);
                } else {
                    LOGE("Failed to open SSH forward channel to %s:%d after retries: %s", remote_host, remote_port, ssh_get_error(session));
                }
                ssh_channel_free(channel);
            }
        }
        pthread_mutex_unlock(&session_mutex);
        
        close(client_sock);
    }
    
    if (listen_sock >= 0) close(listen_sock);
    if (g_listen_sock == listen_sock) g_listen_sock = -1;
    pf_set_state(PF_STATE_EXIT);
    LOGI("TCP port forwarding thread exiting");
    return NULL;
}

// Setup TCP port forwarding
int setup_tcp_port_forwarding(const char* remote_host, int remote_port, int listen_port) {
    if (port_forward_thread != 0) {
        LOGI("TCP port forwarding already running");
        return 0;
    }
    
    port_forward_running = 0;

    // Allocate and populate config for the thread
    struct pf_config* cfg = (struct pf_config*)calloc(1, sizeof(struct pf_config));
    if (!cfg) { LOGE("Failed to allocate pf_config"); return -1; }
    if (remote_host && *remote_host) {
        strncpy(cfg->remote_host, remote_host, sizeof(cfg->remote_host)-1);
        cfg->remote_host[sizeof(cfg->remote_host)-1] = '\0';
    } else {
        strncpy(cfg->remote_host, "127.0.0.1", sizeof(cfg->remote_host)-1);
        cfg->remote_host[sizeof(cfg->remote_host)-1] = '\0';
    }
    cfg->remote_port = remote_port > 0 ? remote_port : 8080;
    cfg->listen_port = listen_port > 0 ? listen_port : cfg->remote_port;

    if (pthread_create(&port_forward_thread, NULL, tcp_port_forward_thread, cfg) != 0) {
        LOGE("Failed to create TCP port forwarding thread");
        free(cfg);
        return -1;
    }
    // Avoid duplicate 'started' log (actual start logged inside thread)
    LOGI("TCP port forwarding thread spawn requested");
    return 0;
}

// Stop TCP port forwarding
void stop_tcp_port_forwarding() {
    if (port_forward_thread != 0) {
        LOGI("stop_tcp_port_forwarding: initiating shutdown (state=%s)", pf_state_str(g_pf_state));
        port_forward_running = 0;
        pf_set_state(PF_STATE_SHUTTING_DOWN);
        // Close listening socket to wake accept()/select
        if (g_listen_sock >= 0) {
            LOGI("stop_tcp_port_forwarding: closing listen socket %d", g_listen_sock);
            shutdown(g_listen_sock, SHUT_RDWR);
            close(g_listen_sock);
            g_listen_sock = -1;
        }
        // Attempt timed join loop for diagnostic logging
        const uint64_t start_ns = monotonic_ns();
        int joined = 0;
#if defined(__ANDROID__) || defined(__linux__)
        // Try non-portable tryjoin to avoid hard hang (best effort)
        for (int attempt = 0; attempt < 50; ++attempt) { // ~5s max
#ifdef __GLIBC__
            int tj = pthread_tryjoin_np(port_forward_thread, NULL);
#else
            int tj = -1; // fallback path if tryjoin not available
#endif
            if (tj == 0) { joined = 1; break; }
            struct timespec slp = {0, 100 * 1000 * 1000}; // 100ms
            nanosleep(&slp, NULL);
            if ((attempt % 10) == 0) {
                uint64_t elapsed_ms = (monotonic_ns() - start_ns)/1000000ull;
                LOGI("stop_tcp_port_forwarding: waiting join... elapsed=%llums state=%s", (unsigned long long)elapsed_ms, pf_state_str(g_pf_state));
            }
            if (g_pf_state == PF_STATE_EXIT) { break; }
        }
#endif
        if (!joined) {
            // Fallback blocking join (should be quick now or we log after timeout)
            LOGI("stop_tcp_port_forwarding: performing final blocking join (state=%s)", pf_state_str(g_pf_state));
            pthread_join(port_forward_thread, NULL);
        }
        port_forward_thread = 0;
        uint64_t total_ms = (monotonic_ns() - start_ns)/1000000ull;
    LOGI("stop_tcp_port_forwarding: completed in %llums final_state=%s", (unsigned long long)total_ms, pf_state_str(g_pf_state));
        LOGI("TCP port forwarding stopped");
    }
}

// Simplified mock-friendly connect function
JNIEXPORT jboolean JNICALL Java_com_example_sshtunnel_SshTunnelService_connectToServer(
    JNIEnv *env, jobject obj, jstring host, jint port, jstring username, jstring password) {
    
    (void)obj; // Suppress unused parameter warning
    
    const char *host_str = (*env)->GetStringUTFChars(env, host, 0);
    const char *username_str = (*env)->GetStringUTFChars(env, username, 0);
    const char *password_str = (*env)->GetStringUTFChars(env, password, 0);
    
    LOGI("Starting SSH connection to %s:%d as %s", host_str, port, username_str);
    
    pthread_mutex_lock(&session_mutex);
    
    if (session != NULL) {
        LOGW("Session already exists, disconnecting first");
        ssh_disconnect(session);
        ssh_free(session);
        session = NULL;
    }
    
    session = ssh_new();
    if (session == NULL) {
        LOGE("Failed to create SSH session");
        pthread_mutex_unlock(&session_mutex);
        (*env)->ReleaseStringUTFChars(env, host, host_str);
        (*env)->ReleaseStringUTFChars(env, username, username_str);
        (*env)->ReleaseStringUTFChars(env, password, password_str);
        return JNI_FALSE;
    }
    
    // Set connection options
    ssh_options_set(session, SSH_OPTIONS_HOST, host_str);
    ssh_options_set(session, SSH_OPTIONS_PORT, &port);
    ssh_options_set(session, SSH_OPTIONS_USER, username_str);
    
    // Set some crypto-related options to trigger initialization
    const char *ciphers = "aes128-ctr,aes192-ctr,aes256-ctr";
    ssh_options_set(session, SSH_OPTIONS_CIPHERS_C_S, ciphers);
    ssh_options_set(session, SSH_OPTIONS_CIPHERS_S_C, ciphers);
    
    const char *kex = "diffie-hellman-group14-sha256,ecdh-sha2-nistp256";
    ssh_options_set(session, SSH_OPTIONS_KEY_EXCHANGE, kex);
    
    ssh_set_blocking(session, 1);

#ifndef USE_LIBSSH_MOCK
    // CRITICAL: Force entropy initialization before any crypto operations
    LOGI("Forcing entropy initialization before ssh_connect");
    unsigned char entropy_buf[64];
    size_t entropy_len;
    
    // Generate fresh entropy to ensure /dev/urandom is accessible
    if (android_entropy_source(NULL, entropy_buf, sizeof(entropy_buf), &entropy_len) == 0) {
        LOGI("Pre-connect entropy generation successful: %zu bytes", entropy_len);
        
        // Try to seed randomness manually if possible
    // Extra entropy rounds (defensive; may be redundant when using OpenSSL)
        for (int i = 0; i < 3; i++) {
            unsigned char more_entropy[32];
            size_t more_len;
            if (android_entropy_source(NULL, more_entropy, sizeof(more_entropy), &more_len) == 0) {
                LOGI("Additional entropy round %d: %zu bytes", i+1, more_len);
            }
        }
    } else {
        LOGE("Critical: Unable to generate entropy before ssh_connect - expect crashes");
    }
#else
    LOGI("Mock mode: Skipping entropy initialization");
#endif

    LOGI("Attempting SSH connection to %s:%d with enhanced crypto options", host_str, port);
    int connection = ssh_connect(session);
    if (connection != SSH_OK) {
        LOGE("SSH connection failed: %s", ssh_get_error(session));
        ssh_free(session);
        session = NULL;
        pthread_mutex_unlock(&session_mutex);
        (*env)->ReleaseStringUTFChars(env, host, host_str);
        (*env)->ReleaseStringUTFChars(env, username, username_str);
        (*env)->ReleaseStringUTFChars(env, password, password_str);
        return JNI_FALSE;
    }
    
    LOGI("SSH connection established successfully");
    
    // Authenticate with password
    int auth = ssh_userauth_password(session, username_str, password_str);
    if (auth != SSH_AUTH_SUCCESS) {
        LOGE("SSH authentication failed: %s", ssh_get_error(session));
        ssh_disconnect(session);
        ssh_free(session);
        session = NULL;
        pthread_mutex_unlock(&session_mutex);
        (*env)->ReleaseStringUTFChars(env, host, host_str);
        (*env)->ReleaseStringUTFChars(env, username, username_str);
        (*env)->ReleaseStringUTFChars(env, password, password_str);
        return JNI_FALSE;
    }
    
    LOGI("SSH authentication successful");
    
    // Port forwarding will be started later from udp2tcp start with the desired ports
    
    pthread_mutex_unlock(&session_mutex);
    
    (*env)->ReleaseStringUTFChars(env, host, host_str);
    (*env)->ReleaseStringUTFChars(env, username, username_str);
    (*env)->ReleaseStringUTFChars(env, password, password_str);
    
    return JNI_TRUE;
}

#ifndef USE_LIBSSH_MOCK
// Connecting with SSH key authentication
JNIEXPORT jboolean JNICALL Java_com_example_sshtunnel_SshTunnelService_connectWithKey(
    JNIEnv *env, jobject obj, jstring host, jint port, jstring username, jstring private_key_path, jstring passphrase) {
        
    (void)obj; // Suppress unused parameter warning
    
    const char *host_str = (*env)->GetStringUTFChars(env, host, 0);
    const char *username_str = (*env)->GetStringUTFChars(env, username, 0);
    const char *key_path_str = (*env)->GetStringUTFChars(env, private_key_path, 0);
    const char *passphrase_str = passphrase ? (*env)->GetStringUTFChars(env, passphrase, 0) : NULL;
    
    LOGI("Starting SSH connection with key to %s:%d as %s", host_str, port, username_str);
    
    pthread_mutex_lock(&session_mutex);
    
    if (session != NULL) {
        LOGW("Session already exists, disconnecting first");
        ssh_disconnect(session);
        ssh_free(session);
        session = NULL;
    }
    
    session = ssh_new();
    if (session == NULL) {
        LOGE("Failed to create SSH session");
        pthread_mutex_unlock(&session_mutex);
        (*env)->ReleaseStringUTFChars(env, host, host_str);
        (*env)->ReleaseStringUTFChars(env, username, username_str);
        (*env)->ReleaseStringUTFChars(env, private_key_path, key_path_str);
        if (passphrase_str) (*env)->ReleaseStringUTFChars(env, passphrase, passphrase_str);
        return -1;
    }
    
    // Set connection options
    ssh_options_set(session, SSH_OPTIONS_HOST, host_str);
    ssh_options_set(session, SSH_OPTIONS_PORT, &port);
    ssh_options_set(session, SSH_OPTIONS_USER, username_str);
    ssh_set_blocking(session, 1);
    
    LOGI("Attempting SSH connection to %s:%d", host_str, port);
    int connection = ssh_connect(session);
    if (connection != SSH_OK) {
        LOGE("SSH connection failed: %s", ssh_get_error(session));
        ssh_free(session);
        session = NULL;
        pthread_mutex_unlock(&session_mutex);
        (*env)->ReleaseStringUTFChars(env, host, host_str);
        (*env)->ReleaseStringUTFChars(env, username, username_str);
        (*env)->ReleaseStringUTFChars(env, private_key_path, key_path_str);
        if (passphrase_str) (*env)->ReleaseStringUTFChars(env, passphrase, passphrase_str);
        return -1;
    }
    
    LOGI("SSH connection established, authenticating with key");
    
    // Load private key
    ssh_key privkey;
    int key_result = ssh_pki_import_privkey_file(key_path_str, passphrase_str, NULL, NULL, &privkey);
    if (key_result != SSH_OK) {
        LOGE("Failed to load private key from %s: %s", key_path_str, ssh_get_error(session));
        ssh_disconnect(session);
        ssh_free(session);
        session = NULL;
        pthread_mutex_unlock(&session_mutex);
        (*env)->ReleaseStringUTFChars(env, host, host_str);
        (*env)->ReleaseStringUTFChars(env, username, username_str);
        (*env)->ReleaseStringUTFChars(env, private_key_path, key_path_str);
        if (passphrase_str) (*env)->ReleaseStringUTFChars(env, passphrase, passphrase_str);
        return -1;
    }
    
    // Authenticate with private key
    int auth = ssh_userauth_publickey(session, username_str, privkey);
    ssh_key_free(privkey);
    
    if (auth != SSH_AUTH_SUCCESS) {
        LOGE("SSH key authentication failed: %s", ssh_get_error(session));
        ssh_disconnect(session);
        ssh_free(session);
        session = NULL;
        pthread_mutex_unlock(&session_mutex);
        (*env)->ReleaseStringUTFChars(env, host, host_str);
        (*env)->ReleaseStringUTFChars(env, username, username_str);
        (*env)->ReleaseStringUTFChars(env, private_key_path, key_path_str);
        if (passphrase_str) (*env)->ReleaseStringUTFChars(env, passphrase, passphrase_str);
        return -1;
    }
    
    LOGI("SSH key authentication successful");
    
    // Port forwarding will be started later from udp2tcp start with the desired ports
    
    pthread_mutex_unlock(&session_mutex);
    
    (*env)->ReleaseStringUTFChars(env, host, host_str);
    (*env)->ReleaseStringUTFChars(env, username, username_str);
    (*env)->ReleaseStringUTFChars(env, private_key_path, key_path_str);
    if (passphrase_str) (*env)->ReleaseStringUTFChars(env, passphrase, passphrase_str);
    
    return 0;
}
#else
// Mock version of connectWithKey
JNIEXPORT jboolean JNICALL Java_com_example_sshtunnel_SshTunnelService_connectWithKey(
    JNIEnv *env, jobject obj, jstring host, jint port, jstring username, jstring private_key_path, jstring passphrase) {
    
    (void)obj; (void)private_key_path; (void)passphrase; // Suppress unused parameter warnings
    
    const char *host_str = (*env)->GetStringUTFChars(env, host, 0);
    const char *username_str = (*env)->GetStringUTFChars(env, username, 0);
    
    LOGI("Mock SSH key connection to %s:%d as %s", host_str, port, username_str);
    
    (*env)->ReleaseStringUTFChars(env, host, host_str);
    (*env)->ReleaseStringUTFChars(env, username, username_str);
    
    return 0; // Mock success
}
#endif

JNIEXPORT void JNICALL Java_com_example_sshtunnel_SshTunnelService_disconnect(JNIEnv *env, jobject obj) {
    (void)env; (void)obj; // Suppress unused parameter warnings
    
    LOGI("Disconnecting SSH session");
    uint64_t t0 = monotonic_ns();
    
    // Stop TCP port forwarding first
    stop_tcp_port_forwarding();
    
    pthread_mutex_lock(&session_mutex);
    tunnel_active = 0;
    
    if (session != NULL) {
        ssh_disconnect(session);
        ssh_free(session);
        session = NULL;
        LOGI("SSH session disconnected and freed");
    } else {
        LOGW("No active SSH session to disconnect");
    }
    
    pthread_mutex_unlock(&session_mutex);
    uint64_t elapsed_ms = (monotonic_ns() - t0)/1000000ull;
    LOGI("Disconnect sequence finished in %llums", (unsigned long long)elapsed_ms);
}

// Legacy forwardPort removed

// Legacy stopTunnel completely removed

JNIEXPORT jboolean JNICALL Java_com_example_sshtunnel_SshTunnelService_isConnected(JNIEnv *env, jobject obj) {
    (void)env; (void)obj; // Suppress unused parameter warnings
    
    pthread_mutex_lock(&session_mutex);
    jboolean connected = (session != NULL) ? JNI_TRUE : JNI_FALSE;
    pthread_mutex_unlock(&session_mutex);
    
    return connected;
}

// Debug dump JNI: returns internal native state snapshot (for diagnosing disconnect hangs)
JNIEXPORT jstring JNICALL Java_com_example_sshtunnel_SshTunnelService_nativeDebugDump(JNIEnv *env, jobject obj) {
    (void)obj;
    pthread_mutex_lock(&session_mutex);
    int session_present = (session != NULL);
    pthread_mutex_unlock(&session_mutex);
    uint64_t now_ns = monotonic_ns();
    uint64_t state_age_ms = (g_pf_last_state_change_mono_ns > 0) ? (now_ns - g_pf_last_state_change_mono_ns)/1000000ull : 0;
    char buf[512];
    snprintf(buf, sizeof(buf),
             "native_debug:\n session_present=%s\n port_forward_thread=%s\n port_forward_running=%d\n pf_state=%s age_ms=%llu\n listen_sock=%d\n",
             session_present ? "true" : "false",
             port_forward_thread != 0 ? "true" : "false",
             port_forward_running,
             pf_state_str(g_pf_state),
             (unsigned long long)state_age_ms,
             g_listen_sock);
    return (*env)->NewStringUTF(env, buf);
}

// Legacy getUdpBridgeStats removed

// Legacy isUdpBridgeRunning removed

// Legacy resetUdpBridgeStats removed

// Legacy getUdpBridgeClientCount removed

// Legacy TCP manager JNI removed

// udp2tcp JNI section (real implementation only when USE_UDP2TCP defined)
#ifdef USE_UDP2TCP
// Start udp2tcp (initialize + start thread) with explicit 6-parameter contract
JNIEXPORT jint JNICALL Java_com_example_sshtunnel_SshTunnelService_startUdp2Tcp(
    JNIEnv *env, jobject obj,
    jstring j_tcp_connect_host, jint tcp_connect_port,
    jstring j_listen_addr,     jint listen_port,
    jstring j_remote_dst_ip,   jint remote_dst_port) {
    (void)obj;
    const char* tcp_connect_host = j_tcp_connect_host ? (*env)->GetStringUTFChars(env, j_tcp_connect_host, 0) : NULL;
    const char* listen_addr      = j_listen_addr     ? (*env)->GetStringUTFChars(env, j_listen_addr, 0)      : NULL;
    const char* remote_dst_ip    = j_remote_dst_ip   ? (*env)->GetStringUTFChars(env, j_remote_dst_ip, 0)   : NULL;

#ifdef USE_UDP2TCP
    // Configure SSH port forward: local 127.0.0.1:tcp_connect_port -> remote 127.0.0.1:tcp_connect_port
    if (port_forward_thread == 0) {
        if (setup_tcp_port_forwarding("127.0.0.1", tcp_connect_port, tcp_connect_port) != 0) {
            LOGW("Failed to setup TCP port forwarding prior to udp2tcp start");
        }
    }
#endif

    const char* eff_tcp_host = (tcp_connect_host && *tcp_connect_host) ? tcp_connect_host : "127.0.0.1";
    const char* eff_listen    = (listen_addr && *listen_addr) ? listen_addr : "0.0.0.0";
    const char* eff_dst_ip    = (remote_dst_ip && *remote_dst_ip) ? remote_dst_ip : "127.0.0.1";

    LOGI("Starting udp2tcp with params:\n tcp_connect=%s:%d\n listen=%s:%d\n dst_udp=%s:%d",
         eff_tcp_host, tcp_connect_port, eff_listen, listen_port, eff_dst_ip, remote_dst_port);

    int rc = udp2tcp_start(
        eff_tcp_host,                // tcp_connect.host (via SSH forward typically 127.0.0.1)
        tcp_connect_port,            // tcp_connect.port
        eff_listen,                  // listen_addr
        listen_port,                 // listen_port
        eff_dst_ip,                  // remote_dst_ip
        remote_dst_port              // remote_dst_port
    );

    if (j_remote_dst_ip) (*env)->ReleaseStringUTFChars(env, j_remote_dst_ip, remote_dst_ip);
    if (j_listen_addr)   (*env)->ReleaseStringUTFChars(env, j_listen_addr, listen_addr);
    if (j_tcp_connect_host) (*env)->ReleaseStringUTFChars(env, j_tcp_connect_host, tcp_connect_host);
    return rc;
}

// Advanced udp2tcp start (removed)

// Stop udp2tcp (graceful)
JNIEXPORT void JNICALL Java_com_example_sshtunnel_SshTunnelService_stopUdp2Tcp(JNIEnv *env, jobject obj) {
    (void)env; (void)obj;
    if (udp2tcp_is_running()) {
        LOGI("Stopping udp2tcp adapter");
        udp2tcp_stop();
        udp2tcp_cleanup();
    }
}

// Get udp2tcp stats
JNIEXPORT jstring JNICALL Java_com_example_sshtunnel_SshTunnelService_getUdp2TcpStats(JNIEnv *env, jobject obj) {
    (void)obj;
    uint64_t tx_frames=0, rx_frames=0, tx_bytes=0, rx_bytes=0;
    udp2tcp_get_library_stats(&tx_frames, &rx_frames, &tx_bytes, &rx_bytes);
    char buf[256];
    snprintf(buf, sizeof(buf),
             "udp2tcp: running=%s\nTX frames=%llu bytes=%llu\nRX frames=%llu bytes=%llu",
             udp2tcp_is_running() ? "true" : "false",
             (unsigned long long)tx_frames, (unsigned long long)tx_bytes,
             (unsigned long long)rx_frames, (unsigned long long)rx_bytes);
    return (*env)->NewStringUTF(env, buf);
}

// Check if running
JNIEXPORT jboolean JNICALL Java_com_example_sshtunnel_SshTunnelService_isUdp2TcpRunning(JNIEnv *env, jobject obj) {
    (void)env; (void)obj;
    return udp2tcp_is_running() ? JNI_TRUE : JNI_FALSE;
}
#else
// Stub implementations when udp2tcp is not compiled in (avoid missing JNI symbols)
JNIEXPORT jint JNICALL Java_com_example_sshtunnel_SshTunnelService_startUdp2Tcp(
    JNIEnv *env, jobject obj, jstring remote_host, jint remote_port, jint local_udp_port) {
    (void)env; (void)obj; (void)remote_host; (void)remote_port; (void)local_udp_port;
    LOGW("udp2tcp not enabled in this build (startUdp2Tcp)");
    return -1;
}

// Advanced udp2tcp start stub (removed)

JNIEXPORT void JNICALL Java_com_example_sshtunnel_SshTunnelService_stopUdp2Tcp(JNIEnv *env, jobject obj) {
    (void)env; (void)obj;
    LOGW("udp2tcp not enabled in this build (stopUdp2Tcp)");
}

JNIEXPORT jstring JNICALL Java_com_example_sshtunnel_SshTunnelService_getUdp2TcpStats(JNIEnv *env, jobject obj) {
    (void)obj;
    return (*env)->NewStringUTF(env, "udp2tcp disabled (build without USE_UDP2TCP)");
}

JNIEXPORT jboolean JNICALL Java_com_example_sshtunnel_SshTunnelService_isUdp2TcpRunning(JNIEnv *env, jobject obj) {
    (void)env; (void)obj; return JNI_FALSE;
}
#endif // USE_UDP2TCP

// Runtime TLS/OpenSSL self-test to verify that static OpenSSL is correctly linked.
// Returns >0 (length of version string) on success, 0 on failure or if OpenSSL not in use.
JNIEXPORT jint JNICALL Java_com_example_sshtunnel_SshTunnelService_nativeTlsSelfTest(JNIEnv *env, jobject obj) {
    (void)env; (void)obj;
#ifdef USE_OPENSSL
    const char *ver = OpenSSL_version(OPENSSL_VERSION);
    if (!ver) {
        LOGE("nativeTlsSelfTest: OpenSSL_version returned NULL");
        return 0;
    }
    if (strncmp(ver, "OpenSSL", 7) != 0) {
        LOGE("nativeTlsSelfTest: Unexpected version string: %s", ver);
        return 0;
    }
    LOGI("nativeTlsSelfTest: OpenSSL version detected: %s", ver);
    // Touch a couple of symbols to ensure they are linked in (no-op usage)
    unsigned long vnum = OpenSSL_version_num();
    if (vnum == 0) {
        LOGW("nativeTlsSelfTest: OpenSSL_version_num returned 0");
    }
    return (jint)strlen(ver);
#else
    LOGW("nativeTlsSelfTest: OpenSSL not enabled (USE_OPENSSL not defined)");
    return 0;
#endif
}

