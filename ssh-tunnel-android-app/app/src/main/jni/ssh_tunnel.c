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

// Global variable to track port forwarding thread
static pthread_t port_forward_thread = 0;
static int port_forward_running = 0;

// TCP Port forwarding thread function
void* tcp_port_forward_thread(void* arg) {
    (void)arg; // Suppress unused parameter warning
    
    LOGI("TCP port forwarding thread started");
    
    // Create a local socket to listen on port 8080
    int listen_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_sock < 0) {
        LOGE("Failed to create listening socket for port forwarding");
        return NULL;
    }
    
    // Enable socket reuse
    int opt = 1;
    setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    struct sockaddr_in listen_addr;
    memset(&listen_addr, 0, sizeof(listen_addr));
    listen_addr.sin_family = AF_INET;
    listen_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    listen_addr.sin_port = htons(8080);
    
    if (bind(listen_sock, (struct sockaddr*)&listen_addr, sizeof(listen_addr)) < 0) {
        LOGE("Failed to bind to port 8080 for forwarding: %s", strerror(errno));
        close(listen_sock);
        return NULL;
    }
    
    if (listen(listen_sock, 5) < 0) {
        LOGE("Failed to listen on port 8080: %s", strerror(errno));
        close(listen_sock);
        return NULL;
    }
    
    LOGI("TCP port forwarding listening on 127.0.0.1:8080");
    
    port_forward_running = 1;
    
    while (port_forward_running) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        
        int client_sock = accept(listen_sock, (struct sockaddr*)&client_addr, &client_len);
        if (client_sock < 0) {
            if (port_forward_running) {
                LOGE("Failed to accept connection: %s", strerror(errno));
            }
            continue;
        }
        
        LOGI("Accepted TCP connection for forwarding to remote 8080");
        
        // Create SSH channel for forwarding
        pthread_mutex_lock(&session_mutex);
        if (session != NULL) {
            ssh_channel channel = ssh_channel_new(session);
            if (channel != NULL) {
                int rc = ssh_channel_open_forward(channel, "127.0.0.1", 8080, "127.0.0.1", 8080);
                if (rc == SSH_OK) {
                    LOGI("SSH channel opened for TCP forwarding");
                    
                    // Simple forwarding loop (simplified version)
                    char buffer[4096];
                    fd_set read_fds;
                    int max_fd = (client_sock > ssh_get_fd(session)) ? client_sock : ssh_get_fd(session);
                    
                    while (port_forward_running) {
                        FD_ZERO(&read_fds);
                        FD_SET(client_sock, &read_fds);
                        
                        struct timeval tv = {1, 0}; // 1 second timeout
                        int ready = select(max_fd + 1, &read_fds, NULL, NULL, &tv);
                        
                        if (ready <= 0) continue;
                        
                        if (FD_ISSET(client_sock, &read_fds)) {
                            int bytes = recv(client_sock, buffer, sizeof(buffer), 0);
                            if (bytes <= 0) break;
                            
                            ssh_channel_write(channel, buffer, bytes);
                        }
                        
                        // Check for data from SSH channel
                        if (ssh_channel_is_eof(channel)) break;
                        
                        int ssh_bytes = ssh_channel_read_nonblocking(channel, buffer, sizeof(buffer), 0);
                        if (ssh_bytes > 0) {
                            send(client_sock, buffer, ssh_bytes, 0);
                        }
                    }
                    
                    ssh_channel_close(channel);
                } else {
                    LOGE("Failed to open SSH forward channel: %s", ssh_get_error(session));
                }
                ssh_channel_free(channel);
            }
        }
        pthread_mutex_unlock(&session_mutex);
        
        close(client_sock);
    }
    
    close(listen_sock);
    LOGI("TCP port forwarding thread exiting");
    return NULL;
}

// Setup TCP port forwarding
int setup_tcp_port_forwarding() {
    if (port_forward_thread != 0) {
        LOGI("TCP port forwarding already running");
        return 0;
    }
    
    port_forward_running = 0;
    
    if (pthread_create(&port_forward_thread, NULL, tcp_port_forward_thread, NULL) != 0) {
        LOGE("Failed to create TCP port forwarding thread");
        return -1;
    }
    
    LOGI("TCP port forwarding thread started");
    return 0;
}

// Stop TCP port forwarding
void stop_tcp_port_forwarding() {
    if (port_forward_thread != 0) {
        port_forward_running = 0;
        pthread_join(port_forward_thread, NULL);
        port_forward_thread = 0;
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
    
    // Set up TCP port forwarding (used for both legacy bridge or udp2tcp transport)
    if (setup_tcp_port_forwarding() != 0) {
        LOGW("Failed to setup TCP port forwarding, but SSH connection established");
    }
    
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
    
    // Set up TCP port forwarding for UDP Bridge (8080)
    if (setup_tcp_port_forwarding() != 0) {
        LOGW("Failed to setup TCP port forwarding, but SSH connection established");
    }
    
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

// Legacy getUdpBridgeStats removed

// Legacy isUdpBridgeRunning removed

// Legacy resetUdpBridgeStats removed

// Legacy getUdpBridgeClientCount removed

// Legacy TCP manager JNI removed

// udp2tcp JNI section (real implementation only when USE_UDP2TCP defined)
#ifdef USE_UDP2TCP
// Forwarding hint: allow Java to inform native which remote TCP host:port carries udp2tcp
static char g_forward_host[128] = {0};
static int  g_forward_port = 0;
static int  g_forward_local = 0;

JNIEXPORT void JNICALL Java_com_example_sshtunnel_SshTunnelService_nativeSetForwardingHint(
    JNIEnv* env, jobject obj, jstring remote_host, jint remote_port, jint local_port) {
    (void)obj;
    const char* h = (*env)->GetStringUTFChars(env, remote_host, 0);
    strncpy(g_forward_host, h ? h : "", sizeof(g_forward_host)-1);
    g_forward_host[sizeof(g_forward_host)-1] = '\0';
    g_forward_port = remote_port;
    g_forward_local = local_port;
    LOGI("Forwarding hint set remote=%s:%d local_udp=%d", g_forward_host, g_forward_port, g_forward_local);
    if (h) (*env)->ReleaseStringUTFChars(env, remote_host, h);
}
#else
JNIEXPORT void JNICALL Java_com_example_sshtunnel_SshTunnelService_nativeSetForwardingHint(
    JNIEnv* env, jobject obj, jstring remote_host, jint remote_port, jint local_port) {
    (void)env; (void)obj; (void)remote_host; (void)remote_port; (void)local_port;
    LOGW("udp2tcp not enabled: nativeSetForwardingHint ignored");
}
#endif
#ifdef USE_UDP2TCP
// Start udp2tcp (initialize + start thread)
JNIEXPORT jint JNICALL Java_com_example_sshtunnel_SshTunnelService_startUdp2Tcp(
    JNIEnv *env, jobject obj, jstring remote_host, jint remote_port, jint local_udp_port) {
    (void)obj;
    const char* host = (*env)->GetStringUTFChars(env, remote_host, 0);
    LOGI("Starting udp2tcp: remote %s:%d local_udp=%d", host, remote_port, local_udp_port);
    if (udp2tcp_init(host, remote_port, local_udp_port) != 0) {
        LOGE("udp2tcp_init failed");
        (*env)->ReleaseStringUTFChars(env, remote_host, host);
        return -1;
    }
    int rc = udp2tcp_start();
    (*env)->ReleaseStringUTFChars(env, remote_host, host);
    return rc;
}

// Start udp2tcp advanced (with dst ip/port)
JNIEXPORT jint JNICALL Java_com_example_sshtunnel_SshTunnelService_startUdp2TcpAdvanced(
    JNIEnv *env, jobject obj, jstring remote_host, jint remote_port, jint local_udp_port,
    jstring dst_ip, jint dst_port) {
    (void)obj;
    const char* host = (*env)->GetStringUTFChars(env, remote_host, 0);
    const char* dip = dst_ip ? (*env)->GetStringUTFChars(env, dst_ip, 0) : NULL;
    LOGI("Starting udp2tcp (advanced): remote %s:%d local_udp=%d dst=%s:%d", host, remote_port, local_udp_port, dip?dip:"(null)", dst_port);
    if (udp2tcp_init_advanced(host, remote_port, local_udp_port, dip, dst_port) != 0) {
        LOGE("udp2tcp_init_advanced failed");
        if (dst_ip) (*env)->ReleaseStringUTFChars(env, dst_ip, dip);
        (*env)->ReleaseStringUTFChars(env, remote_host, host);
        return -1;
    }
    int rc = udp2tcp_start();
    if (dst_ip) (*env)->ReleaseStringUTFChars(env, dst_ip, dip);
    (*env)->ReleaseStringUTFChars(env, remote_host, host);
    return rc;
}

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

JNIEXPORT jint JNICALL Java_com_example_sshtunnel_SshTunnelService_startUdp2TcpAdvanced(
    JNIEnv *env, jobject obj, jstring remote_host, jint remote_port, jint local_udp_port, jstring dst_ip, jint dst_port) {
    (void)env; (void)obj; (void)remote_host; (void)remote_port; (void)local_udp_port; (void)dst_ip; (void)dst_port;
    LOGW("udp2tcp not enabled in this build (startUdp2TcpAdvanced)");
    return -1;
}

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

