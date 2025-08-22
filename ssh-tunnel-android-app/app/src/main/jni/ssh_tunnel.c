// Cross-platform native core for SSH tunneling and udp2tcp adapter.
#define _GNU_SOURCE
// Android JNI entry points are compiled when SSHTUN_DESKTOP is not defined.
// Desktop CLI wrappers are compiled when SSHTUN_DESKTOP is defined.

#ifndef SSHTUN_DESKTOP
#    include <jni.h>
#endif

#ifdef USE_LIBSSH_MOCK
#    include "libssh_mock.h"
#else
#    include <libssh/callbacks.h>
#    include <libssh/libssh.h>
#endif

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

// udp2tcp integration only (legacy bridge removed)
#include "udp2tcp_client_adapter.h"

// Crypto library includes (OpenSSL only; mbedTLS support removed)
#ifndef USE_LIBSSH_MOCK
#    ifdef USE_OPENSSL
#        include <openssl/crypto.h>
#        include <openssl/err.h>
#        include <openssl/evp.h>
#        include <openssl/rand.h>
#        include <openssl/ssl.h>
#    endif
#endif

#ifndef LOG_TAG
#    define LOG_TAG "SSHTunnelApp"
#endif

#ifdef __ANDROID__
#    include <android/log.h>
#    define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#    define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#    define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)
#else
#    define LOGI(...) \
        do { \
            fprintf(stdout, "[I] " __VA_ARGS__); \
            fprintf(stdout, "\n"); \
        } while (0)
#    define LOGE(...) \
        do { \
            fprintf(stderr, "[E] " __VA_ARGS__); \
            fprintf(stderr, "\n"); \
        } while (0)
#    define LOGW(...) \
        do { \
            fprintf(stderr, "[W] " __VA_ARGS__); \
            fprintf(stderr, "\n"); \
        } while (0)
#endif

// Forward declarations for desktop API (implemented at end of file)
#ifdef SSHTUN_DESKTOP
#    include "ssh_tunnel_api.h"
#endif

static pthread_mutex_t session_mutex = PTHREAD_MUTEX_INITIALIZER;

// Port forward thread state machine for debugging disconnect hangs
enum port_forward_state_e
{
    PF_STATE_NONE = 0,
    PF_STATE_STARTING,
    PF_STATE_LISTEN_READY,
    PF_STATE_WAITING_ACCEPT,
    PF_STATE_CLIENT_ACCEPTED,
    PF_STATE_FORWARD_LOOP,
    PF_STATE_SHUTTING_DOWN,
    PF_STATE_EXIT
};

// Opaque handle definition for desktop API.
struct ssht_handle
{
    ssh_session session; // underlying libssh session
    // per-handle state
    int tunnel_active;
    pthread_t port_forward_thread;
    volatile int port_forward_running;
    int listen_sock;
    volatile enum port_forward_state_e pf_state;
    volatile uint64_t pf_last_state_change_mono_ns;
    // reserved: additional per-handle state (udp2tcp flags) can be added here
};

// NOTE: global handle removed. All callers must provide an explicit ssht_handle *.

// UDP Bridge listener integration
// Legacy only
// Legacy UDP bridge data structures removed

#ifndef USE_LIBSSH_MOCK
static volatile int g_libssh_initialized = 0;

#    ifdef USE_OPENSSL
// Global OpenSSL state flag
static volatile int g_openssl_initialized = 0;
#    endif

// Global libssh log callback (file-scope). Needed to compile with Clang (no nested functions).
static void ssh_android_log_cb(int priority, const char *function, const char *buffer, void *userdata)
{
    (void)userdata;
    // Use INFO level to ensure messages are visible in logcat
#    ifdef __ANDROID__
    __android_log_print(
        ANDROID_LOG_INFO, LOG_TAG, "libssh[%d] %s: %s", priority, function ? function : "", buffer ? buffer : "");
#    else
    LOGI("libssh[%d] %s: %s", priority, function ? function : "", buffer ? buffer : "");
#    endif
}

// Android/Linux entropy source using /dev/urandom
static int android_entropy_source(void *data, unsigned char *output, size_t len, size_t *olen)
{
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

#    ifdef USE_OPENSSL
// Direct OpenSSL initialization as workaround for libssh init failure
static int init_openssl_directly()
{
    if (g_openssl_initialized) {
        LOGI("OpenSSL already initialized");
        return 0;
    }
    LOGI("Initializing OpenSSL directly");
    SSL_library_init();
    SSL_load_error_strings();
    OpenSSL_add_all_algorithms();
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
#    endif

#    ifdef USE_OPENSSL
static int force_crypto_init()
{
    return init_openssl_directly();
}
#    else
static int force_crypto_init()
{
    return 0;
}
#    endif

// Enhanced initialization with entropy source
static int init_libssh_with_entropy()
{
    LOGI("Attempting to initialize libssh with custom entropy source");
    if (force_crypto_init() != 0) {
        LOGE("Failed to force crypto initialization");
        return -1;
    }
    unsigned char entropy_buf[32];
    size_t entropy_len;
    if (android_entropy_source(NULL, entropy_buf, sizeof(entropy_buf), &entropy_len) == 0) {
        LOGI("Successfully generated %zu bytes of entropy", entropy_len);
    } else {
        LOGW("Failed to generate entropy, proceeding without custom source");
    }
    int ret = ssh_init();
    if (ret == SSH_OK) {
        LOGI("ssh_init succeeded");
        return 1; // Success
    } else {
        LOGW("ssh_init failed with code %d, proceeding with manual crypto", ret);
        return -1; // Failed but will continue with manual crypto
    }
}
#endif // !USE_LIBSSH_MOCK

#ifndef SSHTUN_DESKTOP
JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM *vm, void *reserved)
{
    (void)vm;
    (void)reserved; // Suppress unused parameter warnings
    // Avoid process kill on SIGPIPE when writing to a closed socket/channel
    signal(SIGPIPE, SIG_IGN);
#    ifndef USE_LIBSSH_MOCK
    LOGI("JNI_OnLoad: Starting libssh initialization");
    LOGI("JNI_OnLoad: Setting up libssh logging");
    ssh_set_log_callback(ssh_android_log_cb);
    ssh_set_log_level(SSH_LOG_TRACE);
    LOGI("JNI_OnLoad: libssh logging configured");
    LOGI("JNI_OnLoad: Attempting enhanced initialization with entropy");
    LOGI("JNI_OnLoad: Testing libssh version info");
    const char *version = ssh_version(0);
    if (version) {
        LOGI("JNI_OnLoad: libssh version: %s", version);
    } else {
        LOGE("JNI_OnLoad: Failed to get libssh version");
    }
    LOGI("JNI_OnLoad: Testing ssh_new() without init");
    ssh_session test_session = ssh_new();
    if (test_session) {
        LOGI("JNI_OnLoad: ssh_new() succeeded without init");
        ssh_free(test_session);
    } else {
        LOGE("JNI_OnLoad: ssh_new() failed without init");
    }
    int init_result = init_libssh_with_entropy();
    g_libssh_initialized = init_result;
    if (init_result == 1) {
        LOGI("JNI_OnLoad: libssh initialized successfully with entropy");
    } else {
        LOGI("JNI_OnLoad: Proceeding without global ssh_init - will try per-session initialization");
    }
#    else
    LOGI("JNI_OnLoad: Mock mode - skipping libssh initialization");
#    endif
    return JNI_VERSION_1_6;
}

#    ifndef USE_LIBSSH_MOCK
JNIEXPORT void JNICALL JNI_OnUnload(JavaVM *vm, void *reserved)
{
    (void)vm;
    (void)reserved; // Suppress unused parameter warnings
    if (g_libssh_initialized) {
        ssh_finalize();
        g_libssh_initialized = 0;
    }
#        ifdef USE_OPENSSL
    if (g_openssl_initialized) {
        EVP_cleanup();
        ERR_free_strings();
        g_openssl_initialized = 0;
    }
#        endif
    LOGI("JNI_OnUnload: Cleanup completed");
}
#    else
JNIEXPORT void JNICALL JNI_OnUnload(JavaVM *vm, void *reserved)
{
    (void)vm;
    (void)reserved; // Suppress unused parameter warnings
    LOGI("JNI_OnUnload: Mock mode - no cleanup needed");
}
#    endif
#endif // SSHTUN_DESKTOP

// Port forward configuration passed to thread at start (no globals)
struct pf_config
{
    char remote_host[128]; // SSH remote host for direct-tcpip (typically 127.0.0.1 on server)
    int remote_port; // server-side TCP destination port
    char listen_host[64]; // local address we bind to (e.g. 127.0.0.1 or 0.0.0.0)
    int listen_port; // local <listen_host>:<port> we expose
};

// per-handle PF state stored in ssht_handle

static uint64_t monotonic_ns()
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static const char *pf_state_str(enum port_forward_state_e st)
{
    switch (st) {
        case PF_STATE_NONE:
            return "NONE";
        case PF_STATE_STARTING:
            return "STARTING";
        case PF_STATE_LISTEN_READY:
            return "LISTEN_READY";
        case PF_STATE_WAITING_ACCEPT:
            return "WAITING_ACCEPT";
        case PF_STATE_CLIENT_ACCEPTED:
            return "CLIENT_ACCEPTED";
        case PF_STATE_FORWARD_LOOP:
            return "FORWARD_LOOP";
        case PF_STATE_SHUTTING_DOWN:
            return "SHUTTING_DOWN";
        case PF_STATE_EXIT:
            return "EXIT";
        default:
            return "?";
    }
}

// Thread argument: carries pf_config and the handle the thread serves
struct pf_thread_arg
{
    struct pf_config *cfg;
    struct ssht_handle *h;
};

static void pf_set_state(struct ssht_handle *h, enum port_forward_state_e st)
{
    if (h) {
        h->pf_state = st;
        h->pf_last_state_change_mono_ns = monotonic_ns();
    }
    LOGI("PortForwardState -> %s", pf_state_str(st));
}

// Forward buffer (simple dynamic heap buffer for partial writes)
struct forward_buffer
{
    char *data;
    size_t size;
    size_t off;
};

static void fb_init(struct forward_buffer *fb)
{
    fb->data = NULL;
    fb->size = fb->off = 0;
}

static void fb_dispose(struct forward_buffer *fb)
{
    if (fb->data)
        free(fb->data);
    fb->data = NULL;
    fb->size = fb->off = 0;
}

// Append data, performing compaction; returns 0 on success, -1 on OOM/overflow
static int fb_append(struct forward_buffer *fb, const char *src, size_t len, size_t max_cap)
{
    if (len == 0)
        return 0;
    size_t pending = fb->size - fb->off;
    if (pending + len > max_cap)
        return -1;
    if (fb->off > 0) { // compact
        if (pending > 0)
            memmove(fb->data, fb->data + fb->off, pending);
        fb->size = pending;
        fb->off = 0;
    }
    char *nd = (char *)realloc(fb->data, fb->size + len);
    if (!nd)
        return -1;
    fb->data = nd;
    memcpy(fb->data + fb->size, src, len);
    fb->size += len;
    return 0;
}

// Flush buffer to SSH channel (non-blocking); connection_active becomes 0 on fatal error
static void fb_flush_to_ssh(
    struct forward_buffer *fb, ssh_channel channel, int *connection_active, volatile int *running)
{
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
static void fb_flush_to_client(
    struct forward_buffer *fb, int client_sock, int *connection_active, volatile int *running)
{
    while (*running && *connection_active && fb->off < fb->size) {
        ssize_t s = send(client_sock, fb->data + fb->off, fb->size - fb->off, 0);
        if (s < 0) {
            if (errno == EWOULDBLOCK || errno == EAGAIN)
                break;
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

// Argument structure for per-connection forwarding thread
struct pf_conn_arg {
    struct ssht_handle *h;
    int client_sock;
    char remote_host[128];
    char listen_host[64];
    int remote_port;
    int listen_port;
    volatile int *running;
};

// Per-connection forwarding thread. Each accepted client gets its own thread so that slow
// or blocked connections do not stall new accepts. Note: libssh session objects are not
// guaranteed to be fully thread-safe; we therefore serialize libssh channel operations
// using the existing global session_mutex around each call that touches the channel/session.
static void *pf_connection_thread(void *arg)
{
    struct pf_conn_arg *carg = (struct pf_conn_arg *)arg;
    if (!carg)
        return NULL;
    struct ssht_handle *h = carg->h;
    int client_sock = carg->client_sock;
    const char *remote_host = carg->remote_host;
    const char *listen_host = carg->listen_host;
    int remote_port = carg->remote_port;
    int listen_port = carg->listen_port;
    volatile int *running = carg->running;

    LOGI("PF connection thread started local=%s:%d -> remote=%s:%d", listen_host, listen_port, remote_host, remote_port);

    // Create channel
    pthread_mutex_lock(&session_mutex);
    ssh_channel channel = (h && h->session) ? ssh_channel_new(h->session) : NULL;
    pthread_mutex_unlock(&session_mutex);
    if (!channel) {
        LOGE("Connection thread: failed to allocate channel");
        close(client_sock);
        free(carg);
        return NULL;
    }

    int rc = SSH_ERROR;
    const int MAX_OPEN_ATTEMPTS = 10;
    const int RETRY_SLEEP_MS = 200;
    for (int attempt = 1; attempt <= MAX_OPEN_ATTEMPTS && *running; ++attempt) {
        pthread_mutex_lock(&session_mutex);
        rc = ssh_channel_open_forward(channel, remote_host, remote_port, listen_host, listen_port);
        pthread_mutex_unlock(&session_mutex);
        if (rc == SSH_OK) {
            if (attempt > 1) {
                LOGI("Connection thread: channel opened after %d attempts", attempt);
            }
            break;
        }
        const char *err = NULL;
        pthread_mutex_lock(&session_mutex);
        if (h && h->session)
            err = ssh_get_error(h->session);
        pthread_mutex_unlock(&session_mutex);
        if (err) {
            LOGW("Connection thread open_forward attempt %d/%d failed: %s", attempt, MAX_OPEN_ATTEMPTS, err);
            if ((strstr(err, "Connection refused") != NULL || strstr(err, "connection refused") != NULL) && attempt < MAX_OPEN_ATTEMPTS) {
                struct timespec ts;
                ts.tv_sec = RETRY_SLEEP_MS / 1000;
                ts.tv_nsec = (RETRY_SLEEP_MS % 1000) * 1000000L;
                nanosleep(&ts, NULL);
                continue;
            }
        } else {
            LOGW("Connection thread open_forward attempt %d/%d failed (no error string)", attempt, MAX_OPEN_ATTEMPTS);
        }
    }
    if (rc != SSH_OK) {
        pthread_mutex_lock(&session_mutex);
        const char *err = (h && h->session) ? ssh_get_error(h->session) : "(no session)";
        pthread_mutex_unlock(&session_mutex);
        LOGE("Connection thread: failed to open channel to %s:%d: %s", remote_host, remote_port, err);
        pthread_mutex_lock(&session_mutex);
        ssh_channel_free(channel);
        pthread_mutex_unlock(&session_mutex);
        close(client_sock);
        free(carg);
        return NULL;
    }

    pthread_mutex_lock(&session_mutex);
    ssh_channel_set_blocking(channel, 0);
    pthread_mutex_unlock(&session_mutex);

    int flags = fcntl(client_sock, F_GETFL, 0);
    if (flags >= 0)
        fcntl(client_sock, F_SETFL, flags | O_NONBLOCK);

    char buffer[4096];
    fd_set read_fds;
    pthread_mutex_lock(&session_mutex);
    int session_fd = (h && h->session) ? ssh_get_fd(h->session) : -1;
    pthread_mutex_unlock(&session_mutex);
    if (session_fd < 0) {
        LOGW("Connection thread: ssh_get_fd <0; progress may stall");
    }
    int max_fd = (client_sock > session_fd) ? client_sock : session_fd;
    uint64_t last_tick_sec = 0;
    int connection_active = 1;
    struct forward_buffer to_ssh, to_client;
    fb_init(&to_ssh);
    fb_init(&to_client);
    const size_t MAX_BUFFER_CAP = 256 * 1024;

    while (*running && connection_active) {
        FD_ZERO(&read_fds);
        FD_SET(client_sock, &read_fds);
        if (session_fd >= 0)
            FD_SET(session_fd, &read_fds);
        struct timeval tv = {1, 0};
        int ready = select(max_fd + 1, &read_fds, NULL, NULL, &tv);
        if (ready < 0) {
            if (errno == EINTR)
                continue;
            LOGE("Connection thread: select error: %s", strerror(errno));
            break;
        }
        time_t now_sec = time(NULL);
        if (now_sec != (time_t)last_tick_sec) {
            last_tick_sec = (uint64_t)now_sec;
            pthread_mutex_lock(&session_mutex);
            int connected = (h && h->session && ssh_is_connected(h->session));
            pthread_mutex_unlock(&session_mutex);
            if (!connected)
                break;
            if (to_ssh.size > to_ssh.off) {
                pthread_mutex_lock(&session_mutex);
                fb_flush_to_ssh(&to_ssh, channel, &connection_active, running);
                pthread_mutex_unlock(&session_mutex);
            }
            if (to_client.size > to_client.off)
                fb_flush_to_client(&to_client, client_sock, &connection_active, running);
        }
        if (FD_ISSET(client_sock, &read_fds)) {
            for (;;) {
                int bytes = recv(client_sock, buffer, sizeof(buffer), 0);
                if (bytes == 0) { connection_active = 0; break; }
                else if (bytes < 0) {
                    if (errno == EWOULDBLOCK || errno == EAGAIN) break;
                    LOGE("Connection thread: recv error: %s", strerror(errno));
                    connection_active = 0; break;
                }
                if (fb_append(&to_ssh, buffer, (size_t)bytes, MAX_BUFFER_CAP) < 0) {
                    LOGE("Connection thread: to_ssh overflow");
                    connection_active = 0; break;
                }
                pthread_mutex_lock(&session_mutex);
                fb_flush_to_ssh(&to_ssh, channel, &connection_active, running);
                pthread_mutex_unlock(&session_mutex);
                if (to_ssh.size - to_ssh.off > 64 * 1024) break;
            }
        }
        if (session_fd >= 0 && FD_ISSET(session_fd, &read_fds)) {
            if (to_ssh.size > to_ssh.off) {
                pthread_mutex_lock(&session_mutex);
                fb_flush_to_ssh(&to_ssh, channel, &connection_active, running);
                pthread_mutex_unlock(&session_mutex);
            }
        }
        while (*running && connection_active) {
            pthread_mutex_lock(&session_mutex);
            int ssh_bytes = ssh_channel_read_nonblocking(channel, buffer, sizeof(buffer), 0);
            pthread_mutex_unlock(&session_mutex);
            if (ssh_bytes > 0) {
                if (fb_append(&to_client, buffer, (size_t)ssh_bytes, MAX_BUFFER_CAP) < 0) {
                    LOGE("Connection thread: to_client overflow");
                    connection_active = 0; break;
                }
                fb_flush_to_client(&to_client, client_sock, &connection_active, running);
                continue;
            } else if (ssh_bytes == 0 || ssh_bytes == SSH_AGAIN) {
                break; // no more right now
            } else if (ssh_bytes == SSH_EOF) {
                LOGI("Connection thread: SSH EOF");
                connection_active = 0; break;
            } else if (ssh_bytes == SSH_ERROR) {
                LOGE("Connection thread: SSH read error");
                connection_active = 0; break;
            }
        }
        if (to_client.size > to_client.off)
            fb_flush_to_client(&to_client, client_sock, &connection_active, running);
        pthread_mutex_lock(&session_mutex);
        int closed = (!ssh_channel_is_open(channel) || ssh_channel_is_closed(channel));
        pthread_mutex_unlock(&session_mutex);
        if (closed) {
            LOGI("Connection thread: channel closed by remote");
            break;
        }
    }
    fb_dispose(&to_ssh);
    fb_dispose(&to_client);
    pthread_mutex_lock(&session_mutex);
    ssh_channel_close(channel);
    ssh_channel_free(channel);
    pthread_mutex_unlock(&session_mutex);
    close(client_sock);
    LOGI("PF connection thread exiting local=%s:%d -> remote=%s:%d", listen_host, listen_port, remote_host, remote_port);
    free(carg);
    return NULL;
}

// TCP Port forwarding thread function
void *tcp_port_forward_thread(void *arg)
{
    struct pf_thread_arg *targ = (struct pf_thread_arg *)arg;
    struct pf_config *cfg = targ ? targ->cfg : NULL;
    struct ssht_handle *h = targ ? targ->h : NULL;
    LOGI("TCP port forwarding thread started");
    pf_set_state(h, PF_STATE_STARTING);
    int listen_port = cfg ? cfg->listen_port : 8080;
    int remote_port = cfg ? cfg->remote_port : 8080;
    char remote_host_buf[128];
    char listen_host_buf[64];
    if (cfg && cfg->remote_host[0] != '\0') {
        strncpy(remote_host_buf, cfg->remote_host, sizeof(remote_host_buf) - 1);
        remote_host_buf[sizeof(remote_host_buf) - 1] = '\0';
    } else {
        strncpy(remote_host_buf, "127.0.0.1", sizeof(remote_host_buf) - 1);
        remote_host_buf[sizeof(remote_host_buf) - 1] = '\0';
    }
    if (cfg && cfg->listen_host[0] != '\0') {
        strncpy(listen_host_buf, cfg->listen_host, sizeof(listen_host_buf) - 1);
        listen_host_buf[sizeof(listen_host_buf) - 1] = '\0';
    } else {
        strncpy(listen_host_buf, "127.0.0.1", sizeof(listen_host_buf) - 1);
        listen_host_buf[sizeof(listen_host_buf) - 1] = '\0';
    }
    const char *remote_host = remote_host_buf;
    const char *listen_host = listen_host_buf;
    if (targ) {
        free(targ);
        targ = NULL;
    }
    LOGI("Port forward configuration: local=%s:%d -> remote %s:%d", listen_host, listen_port, remote_host, remote_port);
    int listen_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_sock < 0) {
        LOGE("Failed to create listening socket for port forwarding");
        return NULL;
    }
    if (h)
        h->listen_sock = listen_sock;
    int opt = 1;
    setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    struct sockaddr_in listen_addr;
    memset(&listen_addr, 0, sizeof(listen_addr));
    listen_addr.sin_family = AF_INET;
    // Bind to configured listen_host (default 127.0.0.1)
    if (inet_aton(listen_host, &listen_addr.sin_addr) == 0) {
        LOGW("Invalid listen_host '%s', falling back to 127.0.0.1", listen_host);
        listen_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    }
    listen_addr.sin_port = htons(listen_port);
    if (bind(listen_sock, (struct sockaddr *)&listen_addr, sizeof(listen_addr)) < 0) {
        LOGE("Failed to bind to port %d for forwarding: %s", listen_port, strerror(errno));
        close(listen_sock);
        return NULL;
    }
    if (listen(listen_sock, 5) < 0) {
        LOGE("Failed to listen on port %d: %s", listen_port, strerror(errno));
        close(listen_sock);
        return NULL;
    }
    LOGI("TCP port forwarding listening on %s:%d", listen_host, listen_port);
    pf_set_state(h, PF_STATE_LISTEN_READY);
    /* Require an active handle for per-handle port-forwarding state. */
    if (!h) {
        LOGE("TCP port forwarding: no active handle");
        if (listen_sock >= 0)
            close(listen_sock);
        return NULL;
    }
    volatile int *running = &h->port_forward_running;
    h->pf_state = PF_STATE_LISTEN_READY;
    h->pf_last_state_change_mono_ns = monotonic_ns();
    h->port_forward_running = 1;
    while (*running) {
        pf_set_state(h, PF_STATE_WAITING_ACCEPT);
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_sock = accept(listen_sock, (struct sockaddr *)&client_addr, &client_len);
        if (client_sock < 0) {
            if (*running) {
                LOGE("Failed to accept connection: %s", strerror(errno));
            }
            continue;
        }
        LOGI("Accepted TCP connection for forwarding to remote %s:%d (spawning thread)", remote_host, remote_port);
        pf_set_state(h, PF_STATE_CLIENT_ACCEPTED);
        struct pf_conn_arg *carg = (struct pf_conn_arg *)calloc(1, sizeof(struct pf_conn_arg));
        if (!carg) {
            LOGE("Port forward OOM allocating pf_conn_arg");
            close(client_sock);
            continue;
        }
        carg->h = h;
        carg->client_sock = client_sock;
        strncpy(carg->remote_host, remote_host, sizeof(carg->remote_host) - 1);
        carg->remote_host[sizeof(carg->remote_host) - 1] = '\0';
        strncpy(carg->listen_host, listen_host, sizeof(carg->listen_host) - 1);
        carg->listen_host[sizeof(carg->listen_host) - 1] = '\0';
        carg->remote_port = remote_port;
        carg->listen_port = listen_port;
        carg->running = running;
        pthread_t cth;
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
        if (pthread_create(&cth, &attr, pf_connection_thread, carg) != 0) {
            LOGE("Failed to create connection thread: %s", strerror(errno));
            pthread_attr_destroy(&attr);
            close(client_sock);
            free(carg);
            continue;
        }
        pthread_attr_destroy(&attr);
    }
    if (listen_sock >= 0)
        close(listen_sock);
    if (h && h->listen_sock == listen_sock)
        h->listen_sock = -1;
    pf_set_state(h, PF_STATE_EXIT);
    LOGI("TCP port forwarding thread exiting");
    return NULL;
}

// Setup TCP port forwarding
int setup_tcp_port_forwarding(
    struct ssht_handle *h, const char *remote_host, int remote_port, const char *listen_host, int listen_port)
{
    if (!h) {
        LOGE("setup_tcp_port_forwarding: no active handle");
        return -1;
    }
    if (h->port_forward_thread != 0) {
        LOGI("TCP port forwarding already running");
        return 0;
    }
    h->port_forward_running = 0;
    struct pf_config *cfg = (struct pf_config *)calloc(1, sizeof(struct pf_config));
    if (!cfg) {
        LOGE("Failed to allocate pf_config");
        return -1;
    }
    if (remote_host && *remote_host) {
        strncpy(cfg->remote_host, remote_host, sizeof(cfg->remote_host) - 1);
        cfg->remote_host[sizeof(cfg->remote_host) - 1] = '\0';
    } else {
        strncpy(cfg->remote_host, "127.0.0.1", sizeof(cfg->remote_host) - 1);
        cfg->remote_host[sizeof(cfg->remote_host) - 1] = '\0';
    }
    if (listen_host && *listen_host) {
        strncpy(cfg->listen_host, listen_host, sizeof(cfg->listen_host) - 1);
        cfg->listen_host[sizeof(cfg->listen_host) - 1] = '\0';
    } else {
        strncpy(cfg->listen_host, "127.0.0.1", sizeof(cfg->listen_host) - 1);
        cfg->listen_host[sizeof(cfg->listen_host) - 1] = '\0';
    }
    cfg->remote_port = remote_port > 0 ? remote_port : 8080;
    cfg->listen_port = listen_port > 0 ? listen_port : cfg->remote_port;
    struct pf_thread_arg *targ = (struct pf_thread_arg *)calloc(1, sizeof(*targ));
    if (!targ) {
        LOGE("Failed to allocate thread arg for port forwarding");
        free(cfg);
        return -1;
    }
    targ->cfg = cfg;
    targ->h = h;
    if (pthread_create(&h->port_forward_thread, NULL, tcp_port_forward_thread, targ) != 0) {
        LOGE("Failed to create TCP port forwarding thread");
        free(cfg);
        free(targ);
        return -1;
    }
    LOGI("TCP port forwarding thread spawn requested");
    return 0;
}

// Stop TCP port forwarding
void stop_tcp_port_forwarding(struct ssht_handle *h)
{
    if (h && h->port_forward_thread != 0) {
        LOGI("stop_tcp_port_forwarding: initiating shutdown (state=%s)", pf_state_str(h->pf_state));
        h->port_forward_running = 0;
        pf_set_state(h, PF_STATE_SHUTTING_DOWN);
        if (h->listen_sock >= 0) {
            LOGI("stop_tcp_port_forwarding: closing listen socket %d", h->listen_sock);
            shutdown(h->listen_sock, SHUT_RDWR);
            close(h->listen_sock);
            h->listen_sock = -1;
        }
        const uint64_t start_ns = monotonic_ns();
        int joined = 0;
#if defined(__ANDROID__) || defined(__linux__)
#    ifdef __GLIBC__
        for (int attempt = 0; attempt < 50; ++attempt) { // ~5s max
            int tj = pthread_tryjoin_np(h->port_forward_thread, NULL);
            if (tj == 0) {
                joined = 1;
                break;
            }
            struct timespec slp = {0, 100 * 1000 * 1000};
            nanosleep(&slp, NULL);
            if ((attempt % 10) == 0) {
                uint64_t elapsed_ms = (monotonic_ns() - start_ns) / 1000000ull;
                LOGI(
                    "stop_tcp_port_forwarding: waiting join... elapsed=%llums state=%s",
                    (unsigned long long)elapsed_ms,
                    pf_state_str(h ? h->pf_state : PF_STATE_NONE));
            }
            if (h->pf_state == PF_STATE_EXIT) {
                break;
            }
        }
#    endif
#endif
        if (!joined) {
            LOGI("stop_tcp_port_forwarding: performing final blocking join (state=%s)", pf_state_str(h->pf_state));
            pthread_join(h->port_forward_thread, NULL);
        }
        h->port_forward_thread = 0;
        uint64_t total_ms = (monotonic_ns() - start_ns) / 1000000ull;
        LOGI(
            "stop_tcp_port_forwarding: completed in %llums final_state=%s",
            (unsigned long long)total_ms,
            pf_state_str(h->pf_state));
        LOGI("TCP port forwarding stopped");
    }
}

#ifndef SSHTUN_DESKTOP
// Simplified mock-friendly connect function (JNI)
// Returns native pointer (ssht_handle *) as jlong, or 0 on failure
JNIEXPORT jlong JNICALL Java_com_example_sshtunnel_SshTunnelService_connectToServer(
    JNIEnv *env, jobject obj, jstring host, jint port, jstring username, jstring password)
{
    (void)obj;
    const char *host_str = (*env)->GetStringUTFChars(env, host, 0);
    const char *username_str = (*env)->GetStringUTFChars(env, username, 0);
    const char *password_str = (*env)->GetStringUTFChars(env, password, 0);
    LOGI("Starting SSH connection to %s:%d as %s", host_str, port, username_str);
    pthread_mutex_lock(&session_mutex);
    /* per-handle only; no global session maintained */
    ssh_session new_sess = ssh_new();
    if (new_sess == NULL) {
        LOGE("Failed to create SSH session");
        pthread_mutex_unlock(&session_mutex);
        (*env)->ReleaseStringUTFChars(env, host, host_str);
        (*env)->ReleaseStringUTFChars(env, username, username_str);
        (*env)->ReleaseStringUTFChars(env, password, password_str);
        return JNI_FALSE;
    }
    ssh_options_set(new_sess, SSH_OPTIONS_HOST, host_str);
    ssh_options_set(new_sess, SSH_OPTIONS_PORT, &port);
    ssh_options_set(new_sess, SSH_OPTIONS_USER, username_str);
    const char *ciphers = "aes128-ctr,aes192-ctr,aes256-ctr";
    ssh_options_set(new_sess, SSH_OPTIONS_CIPHERS_C_S, ciphers);
    ssh_options_set(new_sess, SSH_OPTIONS_CIPHERS_S_C, ciphers);
    const char *kex = "diffie-hellman-group14-sha256,ecdh-sha2-nistp256";
    ssh_options_set(new_sess, SSH_OPTIONS_KEY_EXCHANGE, kex);
    ssh_set_blocking(new_sess, 1);
#    ifndef USE_LIBSSH_MOCK
    LOGI("Forcing entropy initialization before ssh_connect");
    unsigned char entropy_buf[64];
    size_t entropy_len;
    if (android_entropy_source(NULL, entropy_buf, sizeof(entropy_buf), &entropy_len) == 0) {
        LOGI("Pre-connect entropy generation successful: %zu bytes", entropy_len);
        for (int i = 0; i < 3; i++) {
            unsigned char more_entropy[32];
            size_t more_len;
            if (android_entropy_source(NULL, more_entropy, sizeof(more_entropy), &more_len) == 0) {
                LOGI("Additional entropy round %d: %zu bytes", i + 1, more_len);
            }
        }
    } else {
        LOGE("Critical: Unable to generate entropy before ssh_connect - expect crashes");
    }
#    else
    LOGI("Mock mode: Skipping entropy initialization");
#    endif
    LOGI("Attempting SSH connection to %s:%d with enhanced crypto options", host_str, port);
    int connection = ssh_connect(new_sess);
    if (connection != SSH_OK) {
        LOGE("SSH connection failed: %s", ssh_get_error(new_sess));
        ssh_free(new_sess);
        pthread_mutex_unlock(&session_mutex);
        (*env)->ReleaseStringUTFChars(env, host, host_str);
        (*env)->ReleaseStringUTFChars(env, username, username_str);
        (*env)->ReleaseStringUTFChars(env, password, password_str);
        return JNI_FALSE;
    }
    LOGI("SSH connection established successfully");
    int auth = ssh_userauth_password(new_sess, username_str, password_str);
    if (auth != SSH_AUTH_SUCCESS) {
        LOGE("SSH authentication failed: %s", ssh_get_error(new_sess));
        ssh_disconnect(new_sess);
        ssh_free(new_sess);
        pthread_mutex_unlock(&session_mutex);
        (*env)->ReleaseStringUTFChars(env, host, host_str);
        (*env)->ReleaseStringUTFChars(env, username, username_str);
        (*env)->ReleaseStringUTFChars(env, password, password_str);
        return JNI_FALSE;
    }
    LOGI("SSH authentication successful");
    // store session into a new handle
    struct ssht_handle *h = (struct ssht_handle *)calloc(1, sizeof(*h));
    if (!h) {
        LOGE("Failed to allocate ssht_handle for JNI connection");
        ssh_disconnect(new_sess);
        ssh_free(new_sess);
        pthread_mutex_unlock(&session_mutex);
        (*env)->ReleaseStringUTFChars(env, host, host_str);
        (*env)->ReleaseStringUTFChars(env, username, username_str);
        (*env)->ReleaseStringUTFChars(env, password, password_str);
        return JNI_FALSE;
    }
    h->session = new_sess;
    pthread_mutex_unlock(&session_mutex);
    (*env)->ReleaseStringUTFChars(env, host, host_str);
    (*env)->ReleaseStringUTFChars(env, username, username_str);
    (*env)->ReleaseStringUTFChars(env, password, password_str);
    return (jlong)(uintptr_t)h;
}

#    ifndef USE_LIBSSH_MOCK
// Connecting with SSH key authentication
// Returns native pointer (ssht_handle *) as jlong, or 0 on failure
JNIEXPORT jlong JNICALL Java_com_example_sshtunnel_SshTunnelService_connectWithKey(
    JNIEnv *env, jobject obj, jstring host, jint port, jstring username, jstring private_key_path, jstring passphrase)
{
    (void)obj;
    const char *host_str = (*env)->GetStringUTFChars(env, host, 0);
    const char *username_str = (*env)->GetStringUTFChars(env, username, 0);
    const char *key_path_str = (*env)->GetStringUTFChars(env, private_key_path, 0);
    const char *passphrase_str = passphrase ? (*env)->GetStringUTFChars(env, passphrase, 0) : NULL;
    LOGI("Starting SSH connection with key to %s:%d as %s", host_str, port, username_str);
    pthread_mutex_lock(&session_mutex);
    /* per-handle only; no global session maintained */
    ssh_session new_sess = ssh_new();
    if (new_sess == NULL) {
        LOGE("Failed to create SSH session");
        pthread_mutex_unlock(&session_mutex);
        (*env)->ReleaseStringUTFChars(env, host, host_str);
        (*env)->ReleaseStringUTFChars(env, username, username_str);
        (*env)->ReleaseStringUTFChars(env, private_key_path, key_path_str);
        if (passphrase_str)
            (*env)->ReleaseStringUTFChars(env, passphrase, passphrase_str);
        return -1;
    }
    ssh_options_set(new_sess, SSH_OPTIONS_HOST, host_str);
    ssh_options_set(new_sess, SSH_OPTIONS_PORT, &port);
    ssh_options_set(new_sess, SSH_OPTIONS_USER, username_str);
    ssh_set_blocking(new_sess, 1);
    LOGI("Attempting SSH connection to %s:%d", host_str, port);
    int connection = ssh_connect(new_sess);
    if (connection != SSH_OK) {
        LOGE("SSH connection failed: %s", ssh_get_error(new_sess));
        ssh_free(new_sess);
        pthread_mutex_unlock(&session_mutex);
        (*env)->ReleaseStringUTFChars(env, host, host_str);
        (*env)->ReleaseStringUTFChars(env, username, username_str);
        (*env)->ReleaseStringUTFChars(env, private_key_path, key_path_str);
        if (passphrase_str)
            (*env)->ReleaseStringUTFChars(env, passphrase, passphrase_str);
        return -1;
    }
    LOGI("SSH connection established, authenticating with key");
    ssh_key privkey;
    int key_result = ssh_pki_import_privkey_file(key_path_str, passphrase_str, NULL, NULL, &privkey);
    if (key_result != SSH_OK) {
        LOGE("Failed to load private key from %s: %s", key_path_str, ssh_get_error(new_sess));
        ssh_disconnect(new_sess);
        ssh_free(new_sess);
        pthread_mutex_unlock(&session_mutex);
        (*env)->ReleaseStringUTFChars(env, host, host_str);
        (*env)->ReleaseStringUTFChars(env, username, username_str);
        (*env)->ReleaseStringUTFChars(env, private_key_path, key_path_str);
        if (passphrase_str)
            (*env)->ReleaseStringUTFChars(env, passphrase, passphrase_str);
        return -1;
    }
    int auth = ssh_userauth_publickey(new_sess, username_str, privkey);
    ssh_key_free(privkey);
    if (auth != SSH_AUTH_SUCCESS) {
        LOGE("SSH key authentication failed: %s", ssh_get_error(new_sess));
        ssh_disconnect(new_sess);
        ssh_free(new_sess);
        pthread_mutex_unlock(&session_mutex);
        (*env)->ReleaseStringUTFChars(env, host, host_str);
        (*env)->ReleaseStringUTFChars(env, username, username_str);
        (*env)->ReleaseStringUTFChars(env, private_key_path, key_path_str);
        if (passphrase_str)
            (*env)->ReleaseStringUTFChars(env, passphrase, passphrase_str);
        return -1;
    }
    LOGI("SSH key authentication successful");
    // store to handle
    struct ssht_handle *h = (struct ssht_handle *)calloc(1, sizeof(*h));
    if (!h) {
        LOGE("Failed to allocate ssht_handle for JNI key connection");
        ssh_disconnect(new_sess);
        ssh_free(new_sess);
        pthread_mutex_unlock(&session_mutex);
        (*env)->ReleaseStringUTFChars(env, host, host_str);
        (*env)->ReleaseStringUTFChars(env, username, username_str);
        (*env)->ReleaseStringUTFChars(env, private_key_path, key_path_str);
        if (passphrase_str)
            (*env)->ReleaseStringUTFChars(env, passphrase, passphrase_str);
        return -1;
    }
    h->session = new_sess;
    pthread_mutex_unlock(&session_mutex);
    (*env)->ReleaseStringUTFChars(env, host, host_str);
    (*env)->ReleaseStringUTFChars(env, username, username_str);
    (*env)->ReleaseStringUTFChars(env, private_key_path, key_path_str);
    if (passphrase_str)
        (*env)->ReleaseStringUTFChars(env, passphrase, passphrase_str);
    return (jlong)(uintptr_t)h;
}
#    else
// Mock version of connectWithKey (returns native handle as jlong to match real signature)
JNIEXPORT jlong JNICALL Java_com_example_sshtunnel_SshTunnelService_connectWithKey(
    JNIEnv *env, jobject obj, jstring host, jint port, jstring username, jstring private_key_path, jstring passphrase)
{
    (void)obj;
    (void)private_key_path;
    (void)passphrase;
    const char *host_str = (*env)->GetStringUTFChars(env, host, 0);
    const char *username_str = (*env)->GetStringUTFChars(env, username, 0);
    LOGI("Mock SSH key connection to %s:%d as %s", host_str, port, username_str);
    (*env)->ReleaseStringUTFChars(env, host, host_str);
    (*env)->ReleaseStringUTFChars(env, username, username_str);
    return (jlong)0; // Mock: return null handle
}
#    endif

JNIEXPORT void JNICALL Java_com_example_sshtunnel_SshTunnelService_disconnect(JNIEnv *env, jobject obj, jlong handle)
{
    (void)env;
    (void)obj;
    struct ssht_handle *h = (struct ssht_handle *)(uintptr_t)handle;
    if (!h) {
        LOGW("Disconnect called with null handle");
        return;
    }
    LOGI("Disconnecting SSH session");
    uint64_t t0 = monotonic_ns();
    stop_tcp_port_forwarding(h);
    pthread_mutex_lock(&session_mutex);
    if (h && h->session != NULL) {
        /* mark handle as inactive before tearing down session */
        h->tunnel_active = 0;
        ssh_disconnect(h->session);
        ssh_free(h->session);
        h->session = NULL;
        LOGI("SSH session disconnected and freed (handle)");
        free(h);
    } else {
        LOGW("No active SSH session to disconnect");
    }
    pthread_mutex_unlock(&session_mutex);
    uint64_t elapsed_ms = (monotonic_ns() - t0) / 1000000ull;
    LOGI("Disconnect sequence finished in %llums", (unsigned long long)elapsed_ms);
}

JNIEXPORT jboolean JNICALL
    Java_com_example_sshtunnel_SshTunnelService_isConnected(JNIEnv *env, jobject obj, jlong handle)
{
    (void)env;
    (void)obj;
    struct ssht_handle *h = (struct ssht_handle *)(uintptr_t)handle;
    if (!h)
        return JNI_FALSE;
    pthread_mutex_lock(&session_mutex);
    jboolean connected = (h->session != NULL && ssh_is_connected(h->session)) ? JNI_TRUE : JNI_FALSE;
    pthread_mutex_unlock(&session_mutex);
    return connected;
}

JNIEXPORT jstring JNICALL
    Java_com_example_sshtunnel_SshTunnelService_nativeDebugDump(JNIEnv *env, jobject obj, jlong handle)
{
    (void)obj;
    struct ssht_handle *h = (struct ssht_handle *)(uintptr_t)handle;
    if (!h) {
        return (*env)->NewStringUTF(env, "native_debug:\n handle=NULL\n");
    }
    pthread_mutex_lock(&session_mutex);
    int session_present = (h->session != NULL) ? 1 : 0;
    const char *session_err = NULL;
    if (session_present) {
        session_err = ssh_get_error(h->session);
    }
    pthread_mutex_unlock(&session_mutex);
    uint64_t now_ns = monotonic_ns();
    uint64_t state_age_ms = 0;
    if (h->pf_last_state_change_mono_ns > 0) {
        state_age_ms = (now_ns - h->pf_last_state_change_mono_ns) / 1000000ull;
    }
    char buf[512];
    snprintf(
        buf,
        sizeof(buf),
        "native_debug:\n session_present=%s\n session_error=%s\n port_forward_thread=%s\n port_forward_running=%d\n "
        "pf_state=%s "
        "age_ms=%llu\n listen_sock=%d\n",
        session_present ? "true" : "false",
        session_err ? session_err : "(none)",
        (h->port_forward_thread != 0) ? "true" : "false",
        h->port_forward_running,
        pf_state_str(h->pf_state),
        (unsigned long long)state_age_ms,
        h->listen_sock);
    return (*env)->NewStringUTF(env, buf);
}

#    ifdef USE_UDP2TCP
// Start udp2tcp (initialize + start thread) with explicit 8-parameter contract
JNIEXPORT jint JNICALL Java_com_example_sshtunnel_SshTunnelService_startUdp2Tcp(
    JNIEnv *env,
    jobject obj,
    jlong handle,
    jstring j_remote_bridge_host,
    jint remote_bridge_port,
    jstring j_local_bridge_host,
    jint local_bridge_port,
    jstring j_local_udp_host,
    jint local_udp_port,
    jstring j_remote_udp_host,
    jint remote_udp_port)
{
    (void)obj;
    struct ssht_handle *h = (struct ssht_handle *)(uintptr_t)handle;
    if (!h) {
        LOGW("startUdp2Tcp called with null handle");
        return -1;
    }
    const char *remote_bridge_host = j_remote_bridge_host ? (*env)->GetStringUTFChars(env, j_remote_bridge_host, 0)
                                                          : NULL;
    const char *local_bridge_host = j_local_bridge_host ? (*env)->GetStringUTFChars(env, j_local_bridge_host, 0) : NULL;
    const char *local_udp_host = j_local_udp_host ? (*env)->GetStringUTFChars(env, j_local_udp_host, 0) : NULL;
    const char *remote_udp_host = j_remote_udp_host ? (*env)->GetStringUTFChars(env, j_remote_udp_host, 0) : NULL;
    if (h->port_forward_thread == 0) {
        const char *eff_remote_bridge_host = (remote_bridge_host && *remote_bridge_host) ? remote_bridge_host
                                                                                         : "127.0.0.1";
        int eff_remote_bridge_port = (remote_bridge_port > 0) ? remote_bridge_port : local_bridge_port;
        const char *eff_listen_host = (local_bridge_host && *local_bridge_host) ? local_bridge_host : "127.0.0.1";
        if (setup_tcp_port_forwarding(
                h, eff_remote_bridge_host, eff_remote_bridge_port, eff_listen_host, local_bridge_port)
            != 0) {
            LOGW("Failed to setup TCP port forwarding prior to udp2tcp start");
        }
    }
    const char *eff_tcp_host = (local_bridge_host && *local_bridge_host) ? local_bridge_host : "127.0.0.1";
    const char *eff_listen = (local_udp_host && *local_udp_host) ? local_udp_host : "127.0.0.1";
    const char *eff_dst_ip = (remote_udp_host && *remote_udp_host) ? remote_udp_host : "127.0.0.1";
    LOGI(
        "Starting udp2tcp with params:\n remote_bridge=%s:%d\n tcp_connect=%s:%d\n listen=%s:%d\n dst_udp=%s:%d",
        remote_bridge_host ? remote_bridge_host : "127.0.0.1",
        remote_bridge_port,
        eff_tcp_host,
        local_bridge_port,
        eff_listen,
        local_udp_port,
        eff_dst_ip,
        remote_udp_port);
    int rc = udp2tcp_start(
        (remote_bridge_host && *remote_bridge_host) ? remote_bridge_host : "127.0.0.1",
        (remote_bridge_port > 0) ? remote_bridge_port : local_bridge_port,
        eff_tcp_host,
        local_bridge_port,
        eff_listen,
        local_udp_port,
        eff_dst_ip,
        remote_udp_port);
    if (j_remote_udp_host)
        (*env)->ReleaseStringUTFChars(env, j_remote_udp_host, remote_udp_host);
    if (j_local_udp_host)
        (*env)->ReleaseStringUTFChars(env, j_local_udp_host, local_udp_host);
    if (j_local_bridge_host)
        (*env)->ReleaseStringUTFChars(env, j_local_bridge_host, local_bridge_host);
    if (j_remote_bridge_host)
        (*env)->ReleaseStringUTFChars(env, j_remote_bridge_host, remote_bridge_host);
    return rc;
}

// Stop udp2tcp (graceful)
JNIEXPORT void JNICALL Java_com_example_sshtunnel_SshTunnelService_stopUdp2Tcp(JNIEnv *env, jobject obj, jlong handle)
{
    (void)env;
    (void)obj;
    struct ssht_handle *h = (struct ssht_handle *)(uintptr_t)handle;
    if (!h) {
        LOGW("stopUdp2Tcp called with null handle");
        return;
    }
    if (udp2tcp_is_running()) {
        LOGI("Stopping udp2tcp adapter");
        udp2tcp_stop();
        udp2tcp_cleanup();
    }
}

// Get udp2tcp stats
JNIEXPORT jstring JNICALL
    Java_com_example_sshtunnel_SshTunnelService_getUdp2TcpStats(JNIEnv *env, jobject obj, jlong handle)
{
    (void)obj;
    struct ssht_handle *h = (struct ssht_handle *)(uintptr_t)handle;
    if (!h) {
        return (*env)->NewStringUTF(env, "udp2tcp: handle=NULL");
    }
    uint64_t tx_frames = 0, rx_frames = 0, tx_bytes = 0, rx_bytes = 0;
    udp2tcp_get_library_stats(&tx_frames, &rx_frames, &tx_bytes, &rx_bytes);
    char buf[256];
    snprintf(
        buf,
        sizeof(buf),
        "udp2tcp: running=%s\nTX frames=%llu bytes=%llu\nRX frames=%llu bytes=%llu",
        udp2tcp_is_running() ? "true" : "false",
        (unsigned long long)tx_frames,
        (unsigned long long)tx_bytes,
        (unsigned long long)rx_frames,
        (unsigned long long)rx_bytes);
    return (*env)->NewStringUTF(env, buf);
}

// Check if running
JNIEXPORT jboolean JNICALL
    Java_com_example_sshtunnel_SshTunnelService_isUdp2TcpRunning(JNIEnv *env, jobject obj, jlong handle)
{
    (void)env;
    (void)obj;
    struct ssht_handle *h = (struct ssht_handle *)(uintptr_t)handle;
    if (!h)
        return JNI_FALSE;
    return udp2tcp_is_running() ? JNI_TRUE : JNI_FALSE;
}
#    else
// Stubs when udp2tcp is disabled
JNIEXPORT jint JNICALL Java_com_example_sshtunnel_SshTunnelService_startUdp2Tcp(
    JNIEnv *env,
    jobject obj,
    jlong handle,
    jstring j_remote_bridge_host,
    jint remote_bridge_port,
    jstring j_local_bridge_host,
    jint local_bridge_port,
    jstring j_local_udp_host,
    jint local_udp_port,
    jstring j_remote_udp_host,
    jint remote_udp_port)
{
    (void)env;
    (void)obj;
    (void)handle;
    (void)j_remote_bridge_host;
    (void)remote_bridge_port;
    (void)j_local_bridge_host;
    (void)local_bridge_port;
    (void)j_local_udp_host;
    (void)local_udp_port;
    (void)j_remote_udp_host;
    (void)remote_udp_port;
    LOGW("udp2tcp not enabled in this build (startUdp2Tcp, 8-arg stub)");
    return -1;
}

JNIEXPORT void JNICALL Java_com_example_sshtunnel_SshTunnelService_stopUdp2Tcp(JNIEnv *env, jobject obj, jlong handle)
{
    (void)env;
    (void)obj;
    (void)handle;
    LOGW("udp2tcp not enabled in this build (stopUdp2Tcp)");
}

JNIEXPORT jstring JNICALL
    Java_com_example_sshtunnel_SshTunnelService_getUdp2TcpStats(JNIEnv *env, jobject obj, jlong handle)
{
    (void)obj;
    (void)handle;
    return (*env)->NewStringUTF(env, "udp2tcp disabled (build without USE_UDP2TCP)");
}

JNIEXPORT jboolean JNICALL
    Java_com_example_sshtunnel_SshTunnelService_isUdp2TcpRunning(JNIEnv *env, jobject obj, jlong handle)
{
    (void)env;
    (void)obj;
    (void)handle;
    return JNI_FALSE;
}
#    endif // USE_UDP2TCP

// Runtime TLS/OpenSSL self-test to verify that static OpenSSL is correctly linked.
// Returns >0 (length of version string) on success, 0 on failure or if OpenSSL not in use.
JNIEXPORT jint JNICALL Java_com_example_sshtunnel_SshTunnelService_nativeTlsSelfTest(JNIEnv *env, jobject obj)
{
    (void)env;
    (void)obj;
#    ifdef USE_OPENSSL
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
    unsigned long vnum = OpenSSL_version_num();
    if (vnum == 0) {
        LOGW("nativeTlsSelfTest: OpenSSL_version_num returned 0");
    }
    return (jint)strlen(ver);
#    else
    LOGW("nativeTlsSelfTest: OpenSSL not enabled (USE_OPENSSL not defined)");
    return 0;
#    endif
}
#endif // SSHTUN_DESKTOP

#ifdef SSHTUN_DESKTOP
// Desktop CLI wrappers exposing the same core functionality without JNI

ssht_handle *ssht_cli_connect_password(const char *host, int port, const char *username, const char *password)
{
    if (!host || !username || !password || port <= 0) {
        LOGE("ssht_cli_connect_password: invalid arguments");
        return NULL;
    }
    signal(SIGPIPE, SIG_IGN);
#    ifndef USE_LIBSSH_MOCK
    ssh_set_log_callback(ssh_android_log_cb);
    ssh_set_log_level(SSH_LOG_PROTOCOL);
    (void)init_libssh_with_entropy();
#    endif
    LOGI("CLI: connecting to %s:%d as %s (password)", host, port, username);
    pthread_mutex_lock(&session_mutex);
    /* per-handle only; do not maintain global handle in CLI */
    ssh_session new_sess = ssh_new();
    if (!new_sess) {
        pthread_mutex_unlock(&session_mutex);
        LOGE("CLI: ssh_new failed");
        return NULL;
    }
    ssh_options_set(new_sess, SSH_OPTIONS_HOST, host);
    ssh_options_set(new_sess, SSH_OPTIONS_PORT, &port);
    ssh_options_set(new_sess, SSH_OPTIONS_USER, username);
    ssh_set_blocking(new_sess, 1);
    int rc = ssh_connect(new_sess);
    if (rc != SSH_OK) {
        LOGE("CLI: ssh_connect failed: %s", ssh_get_error(new_sess));
        ssh_free(new_sess);
        pthread_mutex_unlock(&session_mutex);
        return NULL;
    }
    rc = ssh_userauth_password(new_sess, username, password);
    if (rc != SSH_AUTH_SUCCESS) {
        LOGE("CLI: password auth failed: %s", ssh_get_error(new_sess));
        ssh_disconnect(new_sess);
        ssh_free(new_sess);
        pthread_mutex_unlock(&session_mutex);
        return NULL;
    }
    pthread_mutex_unlock(&session_mutex);
    LOGI("CLI: SSH connection established");
    ssht_handle *h = (ssht_handle *)calloc(1, sizeof(*h));
    if (!h) {
        LOGE("ssht_cli_connect_password: allocation failed");
        /* keep session in global but return NULL to indicate handle allocation failure */
        return NULL;
    }
    h->session = new_sess;
    return h;
}

ssht_handle *ssht_cli_connect_key(
    const char *host, int port, const char *username, const char *private_key_path, const char *passphrase)
{
    if (!host || !username || !private_key_path || port <= 0) {
        LOGE("ssht_cli_connect_key: invalid arguments");
        return NULL;
    }
    signal(SIGPIPE, SIG_IGN);
#    ifndef USE_LIBSSH_MOCK
    ssh_set_log_callback(ssh_android_log_cb);
    ssh_set_log_level(SSH_LOG_PROTOCOL);
    (void)init_libssh_with_entropy();
#    endif
    LOGI("CLI: connecting to %s:%d as %s (key)", host, port, username);
    pthread_mutex_lock(&session_mutex);
    /* per-handle only; do not maintain global handle in CLI */
    ssh_session new_sess = ssh_new();
    if (!new_sess) {
        pthread_mutex_unlock(&session_mutex);
        LOGE("CLI: ssh_new failed");
        return NULL;
    }
    ssh_options_set(new_sess, SSH_OPTIONS_HOST, host);
    ssh_options_set(new_sess, SSH_OPTIONS_PORT, &port);
    ssh_options_set(new_sess, SSH_OPTIONS_USER, username);
    ssh_set_blocking(new_sess, 1);
    int rc = ssh_connect(new_sess);
    if (rc != SSH_OK) {
        LOGE("CLI: ssh_connect failed: %s", ssh_get_error(new_sess));
        ssh_free(new_sess);
        pthread_mutex_unlock(&session_mutex);
        return NULL;
    }
#    ifndef USE_LIBSSH_MOCK
    ssh_key privkey;
    int key_result = ssh_pki_import_privkey_file(private_key_path, passphrase, NULL, NULL, &privkey);
    if (key_result != SSH_OK) {
        LOGE("CLI: failed to load private key from %s: %s", private_key_path, ssh_get_error(new_sess));
        ssh_disconnect(new_sess);
        ssh_free(new_sess);
        pthread_mutex_unlock(&session_mutex);
        return NULL;
    }
    rc = ssh_userauth_publickey(new_sess, username, privkey);
    ssh_key_free(privkey);
    if (rc != SSH_AUTH_SUCCESS) {
        LOGE("CLI: key auth failed: %s", ssh_get_error(new_sess));
        ssh_disconnect(new_sess);
        ssh_free(new_sess);
        pthread_mutex_unlock(&session_mutex);
        return NULL;
    }
#    else
    (void)private_key_path;
    (void)passphrase;
    rc = SSH_AUTH_SUCCESS;
#    endif
    pthread_mutex_unlock(&session_mutex);
    LOGI("CLI: SSH key authentication successful");
    ssht_handle *h = (ssht_handle *)calloc(1, sizeof(*h));
    if (!h) {
        LOGE("ssht_cli_connect_key: allocation failed");
        return NULL;
    }
    h->session = new_sess;
    return h;
}

void ssht_cli_disconnect(ssht_handle *h)
{
    LOGI("CLI: disconnect requested");
    stop_tcp_port_forwarding(h);
    pthread_mutex_lock(&session_mutex);
    if (h) {
        if (h->session) {
            ssh_disconnect(h->session);
            ssh_free(h->session);
            LOGI("CLI: session disconnected");
        }
        free(h);
    } else {
        LOGW("CLI: no session or handle to disconnect");
    }
    pthread_mutex_unlock(&session_mutex);
}

int ssht_cli_start_port_forward(
    ssht_handle *h, const char *remote_host, int remote_port, const char *listen_host, int listen_port)
{
    if (!remote_host || remote_port <= 0 || listen_port <= 0) {
        LOGE("CLI: start_port_forward invalid args");
        return -1;
    }
    // Ensure a session exists for this handle or globally.
    if (h && !h->session) {
        LOGE("CLI: handle has no session for port forwarding");
        return -1;
    }
    if (!h) {
        LOGE("CLI: no active session for port forwarding");
        return -1;
    }
    return setup_tcp_port_forwarding(h, remote_host, remote_port, listen_host, listen_port);
}

int ssht_cli_start_udp2tcp(
    ssht_handle *h,
    const char *remote_bridge_host,
    int remote_bridge_port,
    const char *local_bridge_host,
    int local_bridge_port,
    const char *local_udp_host,
    int local_udp_port,
    const char *remote_udp_host,
    int remote_udp_port)
{
#    ifdef USE_UDP2TCP
    (void)h;
    /* use per-handle thread id when available; treat missing handle as "no thread" */
    if (!h || h->port_forward_thread == 0) {
        const char *rb = (remote_bridge_host && *remote_bridge_host) ? remote_bridge_host : "127.0.0.1";
        int rb_port = (remote_bridge_port > 0) ? remote_bridge_port : local_bridge_port;
        {
            const char *eff_listen_host = (local_bridge_host && *local_bridge_host) ? local_bridge_host : "127.0.0.1";
            (void)setup_tcp_port_forwarding(h, rb, rb_port, eff_listen_host, local_bridge_port);
        }
    }
    const char *eff_tcp_host = (local_bridge_host && *local_bridge_host) ? local_bridge_host : "127.0.0.1";
    const char *eff_listen = (local_udp_host && *local_udp_host) ? local_udp_host : "127.0.0.1";
    const char *eff_dst_ip = (remote_udp_host && *remote_udp_host) ? remote_udp_host : "127.0.0.1";
    return udp2tcp_start(
        (remote_bridge_host && *remote_bridge_host) ? remote_bridge_host : "127.0.0.1",
        (remote_bridge_port > 0) ? remote_bridge_port : local_bridge_port,
        eff_tcp_host,
        local_bridge_port,
        eff_listen,
        local_udp_port,
        eff_dst_ip,
        remote_udp_port);
#    else
    (void)remote_bridge_host;
    (void)remote_bridge_port;
    (void)local_bridge_host;
    (void)local_bridge_port;
    (void)local_udp_host;
    (void)local_udp_port;
    (void)remote_udp_host;
    (void)remote_udp_port;
    LOGW("CLI: udp2tcp disabled in this build");
    return -1;
#    endif
}

void ssht_cli_stop_udp2tcp(ssht_handle *h)
{
#    ifdef USE_UDP2TCP
    (void)h;
    if (udp2tcp_is_running()) {
        udp2tcp_stop();
        udp2tcp_cleanup();
    }
#    endif
}

int ssht_cli_is_connected(ssht_handle *h)
{
    if (h)
        return (h->session != NULL) ? 1 : 0;
    pthread_mutex_lock(&session_mutex);
    int ok = (h && h->session != NULL) ? 1 : 0;
    pthread_mutex_unlock(&session_mutex);
    return ok;
}

int ssht_cli_is_udp2tcp_running(ssht_handle *h)
{
#    ifdef USE_UDP2TCP
    (void)h;
    return udp2tcp_is_running() ? 1 : 0;
#    else
    (void)h;
    return 0;
#    endif
}

int ssht_cli_get_udp2tcp_stats(
    ssht_handle *h, uint64_t *tx_frames, uint64_t *rx_frames, uint64_t *tx_bytes, uint64_t *rx_bytes)
{
#    ifdef USE_UDP2TCP
    (void)h;
    if (!tx_frames || !rx_frames || !tx_bytes || !rx_bytes)
        return -1;
    udp2tcp_get_library_stats(tx_frames, rx_frames, tx_bytes, rx_bytes);
    return 0;
#    else
    (void)h;
    (void)tx_frames;
    (void)rx_frames;
    (void)tx_bytes;
    (void)rx_bytes;
    return -1;
#    endif
}

#endif // SSHTUN_DESKTOP
