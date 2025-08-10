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

// UDP Bridge Protocol integration
#include "udp_bridge_protocol.h"
#include "udp_listener.h"
#include "tcp_connection_manager.h" // Add TCP Connection Manager

// Crypto library includes for manual crypto initialization
#ifndef USE_LIBSSH_MOCK
#ifdef USE_OPENSSL
#include <openssl/rand.h>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#else
#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/platform.h>
#include <mbedtls/error.h>
#include <mbedtls/threading.h>
#endif
#endif

#define LOG_TAG "SSHTunnelApp"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)

static ssh_session session = NULL;
static int tunnel_active = 0;
static pthread_mutex_t session_mutex = PTHREAD_MUTEX_INITIALIZER;

// UDP Bridge listener integration
static udp_listener_ctx_t* udp_listener = NULL;
static pthread_mutex_t udp_listener_mutex = PTHREAD_MUTEX_INITIALIZER;

// TCP Connection Manager for bridge communication
static tcp_connection_manager_t* tcp_connection_manager = NULL;
static pthread_mutex_t tcp_manager_mutex = PTHREAD_MUTEX_INITIALIZER;

#ifndef USE_LIBSSH_MOCK
static volatile int g_libssh_initialized = 0;

#ifdef USE_OPENSSL
// Global OpenSSL objects for manual crypto initialization
static volatile int g_openssl_initialized = 0;
#else
// Global mbedTLS objects for manual crypto initialization
static mbedtls_entropy_context g_entropy;
static mbedtls_ctr_drbg_context g_ctr_drbg;
static volatile int g_mbedtls_initialized = 0;
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
static int force_crypto_init() {
    return init_openssl_directly();
}
#else
static int init_mbedtls_directly() {
    if (g_mbedtls_initialized) {
        LOGI("mbedTLS already initialized");
        return 0;
    }
    
    LOGI("Initializing mbedTLS directly");
    
    // Initialize mbedTLS entropy and DRBG
    mbedtls_entropy_init(&g_entropy);
    mbedtls_ctr_drbg_init(&g_ctr_drbg);
    
    // Add custom entropy source
    int ret = mbedtls_entropy_add_source(&g_entropy, android_entropy_source, 
                                        NULL, 32, MBEDTLS_ENTROPY_SOURCE_STRONG);
    if (ret != 0) {
        char error_buf[100];
        mbedtls_strerror(ret, error_buf, sizeof(error_buf));
        LOGE("Failed to add entropy source: %s", error_buf);
        return -1;
    }
    
    // Seed the DRBG
    const char *personalization = "SSH_TUNNEL_ANDROID";
    ret = mbedtls_ctr_drbg_seed(&g_ctr_drbg, mbedtls_entropy_func, &g_entropy,
                               (const unsigned char *)personalization, strlen(personalization));
    if (ret != 0) {
        char error_buf[100];
        mbedtls_strerror(ret, error_buf, sizeof(error_buf));
        LOGE("Failed to seed DRBG: %s", error_buf);
        return -1;
    }
    
    g_mbedtls_initialized = 1;
    LOGI("mbedTLS initialized successfully");
    return 0;
}

static int force_crypto_init() {
    if (init_mbedtls_directly() != 0) {
        LOGE("Direct mbedTLS init failed");
        return -1;
    }
    
    return 0;
}
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
        LOGI("ssh_init succeeded with mbedTLS pre-initialized");
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
    
    // Initialize libssh (and underlying crypto/RNG such as mbedTLS) once per process
    LOGI("JNI_OnLoad: Attempting enhanced initialization with entropy");
    
    // Try bypassing ssh_init for now and test direct crypto functionality
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
#else
    if (g_mbedtls_initialized) {
        mbedtls_ctr_drbg_free(&g_ctr_drbg);
        mbedtls_entropy_free(&g_entropy);
        g_mbedtls_initialized = 0;
    }
#endif
    
    LOGI("JNI_OnUnload: Cleanup completed");
}
#else
JNIEXPORT void JNICALL JNI_OnUnload(JavaVM* vm, void* reserved) {
    (void)vm; (void)reserved;  // Suppress unused parameter warnings
    LOGI("JNI_OnUnload: Mock mode - no cleanup needed");
}
#endif

// Simplified mock-friendly connect function
JNIEXPORT jint JNICALL Java_com_example_sshtunnel_SSHTunnelService_connect(
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
        return -1;
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
        // Note: This is a workaround for mbedTLS DRBG not being properly initialized
        // We're attempting to ensure entropy is available before crypto operations
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
        return -1;
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
        return -1;
    }
    
    LOGI("SSH authentication successful");
    pthread_mutex_unlock(&session_mutex);
    
    (*env)->ReleaseStringUTFChars(env, host, host_str);
    (*env)->ReleaseStringUTFChars(env, username, username_str);
    (*env)->ReleaseStringUTFChars(env, password, password_str);
    
    return 0;
}

#ifndef USE_LIBSSH_MOCK
// Connecting with SSH key authentication
JNIEXPORT jint JNICALL Java_com_example_sshtunnel_SSHTunnelService_connectWithKey(
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
    pthread_mutex_unlock(&session_mutex);
    
    (*env)->ReleaseStringUTFChars(env, host, host_str);
    (*env)->ReleaseStringUTFChars(env, username, username_str);
    (*env)->ReleaseStringUTFChars(env, private_key_path, key_path_str);
    if (passphrase_str) (*env)->ReleaseStringUTFChars(env, passphrase, passphrase_str);
    
    return 0;
}
#else
// Mock version of connectWithKey
JNIEXPORT jint JNICALL Java_com_example_sshtunnel_SSHTunnelService_connectWithKey(
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

JNIEXPORT void JNICALL Java_com_example_sshtunnel_SSHTunnelService_disconnect(JNIEnv *env, jobject obj) {
    (void)env; (void)obj; // Suppress unused parameter warnings
    
    LOGI("Disconnecting SSH session");
    
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

// Enhanced tunnel function with UDP bridge integration
JNIEXPORT jint JNICALL Java_com_example_sshtunnel_SSHTunnelService_startTunnel(
    JNIEnv *env, jobject obj, jstring remote_host, jint remote_port, jint local_port) {
    
    (void)obj; // Suppress unused parameter warning
    
    const char *remote_host_str = (*env)->GetStringUTFChars(env, remote_host, 0);
    
    LOGI("Starting UDP bridge tunnel to %s:%d, local port: %d", remote_host_str, remote_port, local_port);
    
    pthread_mutex_lock(&udp_listener_mutex);
    
    // Stop existing listener if running
    if (udp_listener) {
        udp_listener_stop(udp_listener);
        udp_listener_destroy(udp_listener);
        udp_listener = NULL;
    }
    
    // Create new UDP listener
    udp_listener = udp_listener_create(local_port);
    if (!udp_listener) {
        LOGE("Failed to create UDP listener");
        pthread_mutex_unlock(&udp_listener_mutex);
        (*env)->ReleaseStringUTFChars(env, remote_host, remote_host_str);
        return -1;
    }
    
    // Configure bridge server (using remote_host as bridge server)
    if (udp_listener_set_bridge_server(udp_listener, remote_host_str, remote_port) != 0) {
        LOGE("Failed to configure bridge server");
        udp_listener_destroy(udp_listener);
        udp_listener = NULL;
        pthread_mutex_unlock(&udp_listener_mutex);
        (*env)->ReleaseStringUTFChars(env, remote_host, remote_host_str);
        return -1;
    }
    
    // Initialize TCP Connection Manager if not already done
    pthread_mutex_lock(&tcp_manager_mutex);
    if (!tcp_connection_manager) {
        tcp_connection_manager = tcp_connection_manager_create();
        if (!tcp_connection_manager) {
            LOGE("Failed to create TCP Connection Manager");
            pthread_mutex_unlock(&tcp_manager_mutex);
            udp_listener_destroy(udp_listener);
            udp_listener = NULL;
            pthread_mutex_unlock(&udp_listener_mutex);
            (*env)->ReleaseStringUTFChars(env, remote_host, remote_host_str);
            return -1;
        }
        
        // Configure reconnection settings
        tcp_reconnect_config_t reconnect_config;
        reconnect_config.enabled = 1;
        reconnect_config.max_attempts = 10;
        reconnect_config.initial_delay_ms = 1000;
        reconnect_config.max_delay_ms = 30000;
        reconnect_config.backoff_multiplier = 2.0f;
        reconnect_config.jitter_ms = 500;
        tcp_connection_manager_configure_reconnect(tcp_connection_manager, &reconnect_config);
        
        LOGI("TCP Connection Manager created and configured");
    }
    
    // Set protocol context for TCP manager
    if (udp_listener->protocol_ctx) {
        tcp_connection_manager_set_protocol_context(tcp_connection_manager, udp_listener->protocol_ctx);
    }
    
    // Connect TCP manager to bridge server
    if (tcp_connection_manager_connect(tcp_connection_manager, remote_host_str, remote_port) != 0) {
        LOGE("Failed to connect TCP manager to bridge server");
        pthread_mutex_unlock(&tcp_manager_mutex);
        udp_listener_destroy(udp_listener);
        udp_listener = NULL;
        pthread_mutex_unlock(&udp_listener_mutex);
        (*env)->ReleaseStringUTFChars(env, remote_host, remote_host_str);
        return -1;
    }
    
    pthread_mutex_unlock(&tcp_manager_mutex);
    
    // Connect to bridge server (legacy support)
    if (udp_listener_connect_bridge(udp_listener) != 0) {
        LOGE("Failed to connect to bridge server");
        udp_listener_destroy(udp_listener);
        udp_listener = NULL;
        pthread_mutex_unlock(&udp_listener_mutex);
        (*env)->ReleaseStringUTFChars(env, remote_host, remote_host_str);
        return -1;
    }
    
    // Start UDP listener
    if (udp_listener_start(udp_listener) != 0) {
        LOGE("Failed to start UDP listener");
        udp_listener_destroy(udp_listener);
        udp_listener = NULL;
        pthread_mutex_unlock(&udp_listener_mutex);
        (*env)->ReleaseStringUTFChars(env, remote_host, remote_host_str);
        return -1;
    }
    
    tunnel_active = 1;
    pthread_mutex_unlock(&udp_listener_mutex);
    
    (*env)->ReleaseStringUTFChars(env, remote_host, remote_host_str);
    
    LOGI("UDP bridge tunnel started successfully");
    return 0;
}

JNIEXPORT void JNICALL Java_com_example_sshtunnel_SSHTunnelService_stopTunnel(JNIEnv *env, jobject obj) {
    (void)env; (void)obj; // Suppress unused parameter warnings
    
    LOGI("Stopping UDP bridge tunnel");
    
    // Disconnect TCP Connection Manager first
    pthread_mutex_lock(&tcp_manager_mutex);
    if (tcp_connection_manager) {
        tcp_connection_manager_disconnect(tcp_connection_manager);
        LOGI("TCP Connection Manager disconnected");
    }
    pthread_mutex_unlock(&tcp_manager_mutex);
    
    pthread_mutex_lock(&udp_listener_mutex);
    
    if (udp_listener) {
        // Stop and destroy UDP listener
        udp_listener_stop(udp_listener);
        udp_listener_disconnect_bridge(udp_listener);
        udp_listener_destroy(udp_listener);
        udp_listener = NULL;
        LOGI("UDP listener stopped and destroyed");
    }
    
    tunnel_active = 0;
    pthread_mutex_unlock(&udp_listener_mutex);
    
    LOGI("UDP bridge tunnel stopped");
}

JNIEXPORT jboolean JNICALL Java_com_example_sshtunnel_SSHTunnelService_isConnected(JNIEnv *env, jobject obj) {
    (void)env; (void)obj; // Suppress unused parameter warnings
    
    pthread_mutex_lock(&session_mutex);
    jboolean connected = (session != NULL) ? JNI_TRUE : JNI_FALSE;
    pthread_mutex_unlock(&session_mutex);
    
    return connected;
}

// UDP Bridge specific JNI functions

// Get UDP bridge statistics
JNIEXPORT jstring JNICALL Java_com_example_sshtunnel_SSHTunnelService_getUdpBridgeStats(JNIEnv *env, jobject obj) {
    (void)obj; // Suppress unused parameter warning
    
    pthread_mutex_lock(&udp_listener_mutex);
    
    if (!udp_listener) {
        pthread_mutex_unlock(&udp_listener_mutex);
        return (*env)->NewStringUTF(env, "UDP Bridge not active");
    }
    
    uint64_t packets_rx, packets_tx, bytes_rx, bytes_tx;
    uint32_t errors;
    
    udp_listener_get_stats(udp_listener, &packets_rx, &packets_tx, &bytes_rx, &bytes_tx, &errors);
    
    pthread_mutex_unlock(&udp_listener_mutex);
    
    // Format statistics string
    char stats_buffer[512];
    snprintf(stats_buffer, sizeof(stats_buffer),
        "Packets RX: %llu, TX: %llu\nBytes RX: %llu, TX: %llu\nErrors: %u\nStatus: %s",
        (unsigned long long)packets_rx, (unsigned long long)packets_tx,
        (unsigned long long)bytes_rx, (unsigned long long)bytes_tx,
        errors, udp_listener_is_running(udp_listener) ? "Running" : "Stopped");
    
    return (*env)->NewStringUTF(env, stats_buffer);
}

// Check if UDP bridge is running
JNIEXPORT jboolean JNICALL Java_com_example_sshtunnel_SSHTunnelService_isUdpBridgeRunning(JNIEnv *env, jobject obj) {
    (void)env; (void)obj; // Suppress unused parameter warnings
    
    pthread_mutex_lock(&udp_listener_mutex);
    jboolean running = (udp_listener && udp_listener_is_running(udp_listener)) ? JNI_TRUE : JNI_FALSE;
    pthread_mutex_unlock(&udp_listener_mutex);
    
    return running;
}

// Reset UDP bridge statistics
JNIEXPORT void JNICALL Java_com_example_sshtunnel_SSHTunnelService_resetUdpBridgeStats(JNIEnv *env, jobject obj) {
    (void)env; (void)obj; // Suppress unused parameter warnings
    
    pthread_mutex_lock(&udp_listener_mutex);
    
    if (udp_listener) {
        udp_listener_reset_stats(udp_listener);
        LOGI("UDP bridge statistics reset");
    }
    
    pthread_mutex_unlock(&udp_listener_mutex);
}

// Get client count from UDP bridge
JNIEXPORT jint JNICALL Java_com_example_sshtunnel_SSHTunnelService_getUdpBridgeClientCount(JNIEnv *env, jobject obj) {
    (void)env; (void)obj; // Suppress unused parameter warnings
    
    pthread_mutex_lock(&udp_listener_mutex);
    
    jint client_count = 0;
    if (udp_listener && udp_listener->protocol_ctx && udp_listener->protocol_ctx->client_manager) {
        client_count = (jint)client_manager_get_count(udp_listener->protocol_ctx->client_manager);
    }
    
    pthread_mutex_unlock(&udp_listener_mutex);
    
    return client_count;
}

// TCP Connection Manager JNI functions

// Initialize TCP Connection Manager
JNIEXPORT jint JNICALL Java_com_example_sshtunnel_SSHTunnelService_initTcpManager(JNIEnv *env, jobject obj) {
    (void)env; (void)obj; // Suppress unused parameter warnings
    
    pthread_mutex_lock(&tcp_manager_mutex);
    
    if (tcp_connection_manager) {
        LOGW("TCP Connection Manager already initialized");
        pthread_mutex_unlock(&tcp_manager_mutex);
        return 0;
    }
    
    tcp_connection_manager = tcp_connection_manager_create();
    if (!tcp_connection_manager) {
        LOGE("Failed to create TCP Connection Manager");
        pthread_mutex_unlock(&tcp_manager_mutex);
        return -1;
    }
    
    LOGI("TCP Connection Manager initialized successfully");
    pthread_mutex_unlock(&tcp_manager_mutex);
    return 0;
}

// Connect TCP manager to bridge server
JNIEXPORT jint JNICALL Java_com_example_sshtunnel_SSHTunnelService_connectTcpBridge(
    JNIEnv *env, jobject obj, jstring server_host, jint server_port) {
    
    (void)obj; // Suppress unused parameter warning
    
    const char *host_str = (*env)->GetStringUTFChars(env, server_host, 0);
    
    pthread_mutex_lock(&tcp_manager_mutex);
    
    if (!tcp_connection_manager) {
        LOGE("TCP Connection Manager not initialized");
        pthread_mutex_unlock(&tcp_manager_mutex);
        (*env)->ReleaseStringUTFChars(env, server_host, host_str);
        return -1;
    }
    
    LOGI("Connecting TCP bridge to %s:%d", host_str, server_port);
    
    int result = tcp_connection_manager_connect(tcp_connection_manager, host_str, server_port);
    
    pthread_mutex_unlock(&tcp_manager_mutex);
    (*env)->ReleaseStringUTFChars(env, server_host, host_str);
    
    if (result == 0) {
        LOGI("Successfully connected to TCP bridge server");
    } else {
        LOGE("Failed to connect to TCP bridge server");
    }
    
    return result;
}

// Disconnect TCP bridge
JNIEXPORT void JNICALL Java_com_example_sshtunnel_SSHTunnelService_disconnectTcpBridge(JNIEnv *env, jobject obj) {
    (void)env; (void)obj; // Suppress unused parameter warnings
    
    pthread_mutex_lock(&tcp_manager_mutex);
    
    if (tcp_connection_manager) {
        LOGI("Disconnecting TCP bridge");
        tcp_connection_manager_disconnect(tcp_connection_manager);
    }
    
    pthread_mutex_unlock(&tcp_manager_mutex);
}

// Check TCP bridge connection status
JNIEXPORT jboolean JNICALL Java_com_example_sshtunnel_SSHTunnelService_isTcpBridgeConnected(JNIEnv *env, jobject obj) {
    (void)env; (void)obj; // Suppress unused parameter warnings
    
    pthread_mutex_lock(&tcp_manager_mutex);
    
    jboolean connected = JNI_FALSE;
    if (tcp_connection_manager) {
        connected = tcp_connection_manager_is_connected(tcp_connection_manager) ? JNI_TRUE : JNI_FALSE;
    }
    
    pthread_mutex_unlock(&tcp_manager_mutex);
    
    return connected;
}

// Get TCP bridge connection state
JNIEXPORT jstring JNICALL Java_com_example_sshtunnel_SSHTunnelService_getTcpBridgeState(JNIEnv *env, jobject obj) {
    (void)obj; // Suppress unused parameter warning
    
    pthread_mutex_lock(&tcp_manager_mutex);
    
    const char* state_str = "NOT_INITIALIZED";
    if (tcp_connection_manager) {
        tcp_connection_state_t state = tcp_connection_manager_get_state(tcp_connection_manager);
        state_str = tcp_connection_state_string(state);
    }
    
    pthread_mutex_unlock(&tcp_manager_mutex);
    
    return (*env)->NewStringUTF(env, state_str);
}

// Get TCP bridge statistics
JNIEXPORT jstring JNICALL Java_com_example_sshtunnel_SSHTunnelService_getTcpBridgeStats(JNIEnv *env, jobject obj) {
    (void)obj; // Suppress unused parameter warning
    
    pthread_mutex_lock(&tcp_manager_mutex);
    
    if (!tcp_connection_manager) {
        pthread_mutex_unlock(&tcp_manager_mutex);
        return (*env)->NewStringUTF(env, "TCP Connection Manager not initialized");
    }
    
    tcp_connection_stats_t stats = tcp_connection_manager_get_stats(tcp_connection_manager);
    const char* last_error = tcp_connection_manager_get_last_error(tcp_connection_manager);
    
    pthread_mutex_unlock(&tcp_manager_mutex);
    
    // Format statistics string
    char stats_buffer[1024];
    snprintf(stats_buffer, sizeof(stats_buffer),
        "Bytes TX: %llu, RX: %llu\n"
        "Messages TX: %u, RX: %u\n"
        "Reconnects: %u\n"
        "Uptime: %ld seconds\n"
        "Last Activity: %ld\n"
        "Last Error: %s",
        (unsigned long long)stats.bytes_sent,
        (unsigned long long)stats.bytes_received,
        stats.messages_sent,
        stats.messages_received,
        stats.reconnect_count,
        time(NULL) - stats.connection_start_time,
        stats.last_activity,
        last_error);
    
    return (*env)->NewStringUTF(env, stats_buffer);
}

// Configure TCP reconnection
JNIEXPORT void JNICALL Java_com_example_sshtunnel_SSHTunnelService_configureTcpReconnect(
    JNIEnv *env, jobject obj, jboolean enabled, jint max_attempts, jint initial_delay_ms, jint max_delay_ms) {
    
    (void)env; (void)obj; // Suppress unused parameter warnings
    
    pthread_mutex_lock(&tcp_manager_mutex);
    
    if (!tcp_connection_manager) {
        LOGW("TCP Connection Manager not initialized");
        pthread_mutex_unlock(&tcp_manager_mutex);
        return;
    }
    
    tcp_reconnect_config_t config;
    config.enabled = enabled ? 1 : 0;
    config.max_attempts = max_attempts;
    config.initial_delay_ms = initial_delay_ms;
    config.max_delay_ms = max_delay_ms;
    config.backoff_multiplier = 2.0f;
    config.jitter_ms = 500;
    
    tcp_connection_manager_configure_reconnect(tcp_connection_manager, &config);
    
    pthread_mutex_unlock(&tcp_manager_mutex);
    
    LOGI("TCP reconnection configured: enabled=%s, max_attempts=%d", 
         enabled ? "true" : "false", max_attempts);
}

// Send ping through TCP bridge
JNIEXPORT jint JNICALL Java_com_example_sshtunnel_SSHTunnelService_sendTcpBridgePing(JNIEnv *env, jobject obj) {
    (void)env; (void)obj; // Suppress unused parameter warnings
    
    pthread_mutex_lock(&tcp_manager_mutex);
    
    if (!tcp_connection_manager) {
        LOGE("TCP Connection Manager not initialized");
        pthread_mutex_unlock(&tcp_manager_mutex);
        return -1;
    }
    
    int result = tcp_connection_manager_send_ping(tcp_connection_manager);
    
    pthread_mutex_unlock(&tcp_manager_mutex);
    
    return result;
}

// Cleanup TCP Connection Manager
JNIEXPORT void JNICALL Java_com_example_sshtunnel_SSHTunnelService_cleanupTcpManager(JNIEnv *env, jobject obj) {
    (void)env; (void)obj; // Suppress unused parameter warnings
    
    pthread_mutex_lock(&tcp_manager_mutex);
    
    if (tcp_connection_manager) {
        LOGI("Cleaning up TCP Connection Manager");
        tcp_connection_manager_destroy(tcp_connection_manager);
        tcp_connection_manager = NULL;
    }
    
    pthread_mutex_unlock(&tcp_manager_mutex);
}
