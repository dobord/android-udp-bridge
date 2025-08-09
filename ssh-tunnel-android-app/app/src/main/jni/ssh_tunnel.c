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
#endif

#ifndef USE_LIBSSH_MOCK
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
    
    // Test random number generation
    unsigned char test_buf[32];
    if (RAND_bytes(test_buf, sizeof(test_buf)) != 1) {
        LOGE("OpenSSL random number generation test failed");
        return -1;
    }
    
    LOGI("OpenSSL initialized successfully");
    g_openssl_initialized = 1;
    return 0;
}

// Custom crypto initialization function for OpenSSL
static int force_crypto_init() {
    LOGI("Forcing OpenSSL crypto initialization");
    return init_openssl_directly();
}
#else
// Direct mbedTLS initialization as workaround for libssh init failure
static int init_mbedtls_directly() {
    if (g_mbedtls_initialized) {
        LOGI("mbedTLS already initialized");
        return 0;
    }
    
    LOGI("Initializing mbedTLS directly");
    
    // Initialize entropy context
    mbedtls_entropy_init(&g_entropy);
    mbedtls_ctr_drbg_init(&g_ctr_drbg);
    
    // Add our custom entropy source
    int ret = mbedtls_entropy_add_source(&g_entropy, android_entropy_source, 
                                         NULL, 32, MBEDTLS_ENTROPY_SOURCE_STRONG);
    if (ret != 0) {
        char error_buf[100];
        mbedtls_strerror(ret, error_buf, sizeof(error_buf));
        LOGE("Failed to add entropy source: %s (0x%x)", error_buf, ret);
        return -1;
    }
    
    // Seed the DRBG
    const char* personalization = "ssh-tunnel-android";
    ret = mbedtls_ctr_drbg_seed(&g_ctr_drbg, mbedtls_entropy_func, &g_entropy,
                                (const unsigned char*)personalization,
                                strlen(personalization));
    if (ret != 0) {
        char error_buf[100];
        mbedtls_strerror(ret, error_buf, sizeof(error_buf));
        LOGE("Failed to seed DRBG: %s (0x%x)", error_buf, ret);
        mbedtls_entropy_free(&g_entropy);
        mbedtls_ctr_drbg_free(&g_ctr_drbg);
        return -1;
    }
    
    // Test the DRBG
    unsigned char test_buf[32];
    ret = mbedtls_ctr_drbg_random(&g_ctr_drbg, test_buf, sizeof(test_buf));
    if (ret != 0) {
        char error_buf[100];
        mbedtls_strerror(ret, error_buf, sizeof(error_buf));
        LOGE("DRBG test failed: %s (0x%x)", error_buf, ret);
        mbedtls_entropy_free(&g_entropy);
        mbedtls_ctr_drbg_free(&g_ctr_drbg);
        return -1;
    }
    
    LOGI("mbedTLS initialized successfully with direct entropy source");
    
    g_mbedtls_initialized = 1;
    return 0;
}

// Custom crypto initialization function that mimics what libssh should do
static int force_crypto_init() {
    LOGI("Forcing mbedTLS crypto initialization");
    
    // Initialize platform
    int ret = mbedtls_platform_setup(NULL);
    if (ret != 0) {
        LOGE("mbedtls_platform_setup failed: %d", ret);
    }
    
    // Initialize our entropy context
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
    struct ssh_threads_callbacks_struct *cb = ssh_threads_get_default();
    if (cb != NULL) {
        LOGI("Setting thread callbacks");
        if (ssh_threads_set_callbacks(cb) != SSH_OK) {
            LOGE("Failed to set SSH thread callbacks");
            return -1;
        }
    } else {
        LOGE("Failed to get thread callbacks");
        return -1;
    }
    
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
    
    // Clean up crypto library
#ifdef USE_OPENSSL
    if (g_openssl_initialized) {
        EVP_cleanup();
        ERR_free_strings();
        g_openssl_initialized = 0;
        LOGI("OpenSSL cleanup completed");
    }
#else
    if (g_mbedtls_initialized) {
        mbedtls_ctr_drbg_free(&g_ctr_drbg);
        mbedtls_entropy_free(&g_entropy);
        g_mbedtls_initialized = 0;
        LOGI("mbedTLS cleanup completed");
    }
#endif
}
#endif


typedef struct {
    int local_port;
    int remote_port;
    char* remote_host;
} tunnel_params_t;

static void* udp_forward_thread(void* arg) {
    tunnel_params_t* params = (tunnel_params_t*)arg;
    
    int local_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (local_sock < 0) {
        LOGE("Failed to create local UDP socket");
        return NULL;
    }
    
    struct sockaddr_in local_addr;
    memset(&local_addr, 0, sizeof(local_addr));
    local_addr.sin_family = AF_INET;
    local_addr.sin_addr.s_addr = INADDR_ANY;
    local_addr.sin_port = htons(params->local_port);
    
    if (bind(local_sock, (struct sockaddr*)&local_addr, sizeof(local_addr)) < 0) {
        LOGE("Failed to bind local UDP socket");
        close(local_sock);
        return NULL;
    }
    
    LOGI("UDP tunnel started on port %d", params->local_port);
    
    char buffer[4096];
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    
    while (tunnel_active) {
    // reset client_len before each recvfrom call
    client_len = sizeof(client_addr);
        ssize_t received = recvfrom(local_sock, buffer, sizeof(buffer), 0,
                                    (struct sockaddr*)&client_addr, &client_len);
        if (received < 0) {
            LOGE("recvfrom failed: errno=%d", errno);
            break;
        }
        if (received == 0) {
            continue;
        }

        LOGI("Received UDP packet of %zd bytes", received);

    // Forward UDP packet through SSH tunnel
    pthread_mutex_lock(&session_mutex);
    ssh_session sess = session;
    if (sess) {
            ssh_channel channel = ssh_channel_new(sess);
            if (channel == NULL) {
                LOGE("Failed to create SSH channel");
                pthread_mutex_unlock(&session_mutex);
                continue;
            }

            if (ssh_channel_open_forward(channel, params->remote_host,
                                         params->remote_port, "localhost", params->local_port) != SSH_OK) {
                LOGE("Failed to open SSH channel for forwarding: %s", ssh_get_error(sess));
                ssh_channel_free(channel);
                pthread_mutex_unlock(&session_mutex);
                continue;
            }

            int written = ssh_channel_write(channel, buffer, (uint32_t)received);
            if (written <= 0) {
                LOGE("ssh_channel_write failed: %s", ssh_get_error(sess));
                ssh_channel_close(channel);
                ssh_channel_free(channel);
                pthread_mutex_unlock(&session_mutex);
                continue;
            }
            LOGI("Successfully wrote %d bytes to SSH channel", written);

            // Optional: set a small timeout for reading response to avoid blocking indefinitely
            ssh_channel_set_blocking(channel, 1);

            int nbytes = ssh_channel_read_timeout(channel, buffer, sizeof(buffer), 0, 1000);
            if (nbytes > 0) {
                LOGI("Received %d bytes response from SSH channel", nbytes);
                if (sendto(local_sock, buffer, nbytes, 0,
                           (struct sockaddr*)&client_addr, client_len) < 0) {
                    LOGE("sendto failed: errno=%d", errno);
                }
            } else if (nbytes == SSH_ERROR) {
                LOGE("ssh_channel_read_timeout error: %s", ssh_get_error(sess));
            }

            ssh_channel_send_eof(channel);
            ssh_channel_close(channel);
            ssh_channel_free(channel);
            pthread_mutex_unlock(&session_mutex);
        } else {
            // Fallback: echo data back if no SSH session
            LOGI("No SSH session, echoing data back");
            if (sendto(local_sock, buffer, received, 0,
                       (struct sockaddr*)&client_addr, client_len) < 0) {
                LOGE("sendto failed: errno=%d", errno);
            }
            // no session was used
            pthread_mutex_unlock(&session_mutex);
        }
    }
    
    close(local_sock);
    free(params->remote_host);
    free(params);
    return NULL;
}

JNIEXPORT jboolean JNICALL
Java_com_example_sshtunnel_SshTunnelService_connectToServer(JNIEnv *env, jobject obj, jstring host, jint port, jstring username, jstring password) {
    const char *host_str = (*env)->GetStringUTFChars(env, host, 0);
    const char *username_str = (*env)->GetStringUTFChars(env, username, 0);
    const char *password_str = (*env)->GetStringUTFChars(env, password, 0);

    LOGI("Attempting to connect to %s:%d with user %s", host_str, port, username_str);

    pthread_mutex_lock(&session_mutex);
#ifndef USE_LIBSSH_MOCK
    if (g_libssh_initialized == 0) {
        LOGI("Attempting lazy ssh_init()");
        int rc = ssh_init();
        LOGI("Lazy ssh_init() returned %d", rc);
        if (rc != SSH_OK) {
            LOGE("ssh_init (lazy) failed with code %d", rc);
            // Don't give up - try to proceed anyway for modern libssh
            LOGI("Proceeding without ssh_init - testing session creation");
            g_libssh_initialized = -1;
        } else {
            g_libssh_initialized = 1;
            LOGI("Lazy ssh_init() successful");
        }
    } else if (g_libssh_initialized == -1) {
        LOGI("Using libssh without global init (crypto issues detected)");
    }
#endif
    session = ssh_new();
    if (session == NULL) {
        LOGE("Failed to create SSH session");
        pthread_mutex_unlock(&session_mutex);
        (*env)->ReleaseStringUTFChars(env, host, host_str);
        (*env)->ReleaseStringUTFChars(env, username, username_str);
        (*env)->ReleaseStringUTFChars(env, password, password_str);
        return JNI_FALSE;
    }
    
    LOGI("SSH session created successfully, setting options");

    // Force crypto initialization per session as workaround for global init failure
    int verbosity = SSH_LOG_PROTOCOL;
    ssh_options_set(session, SSH_OPTIONS_LOG_VERBOSITY, &verbosity);
    
    // Try to force crypto backend initialization before connect
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

    int auth = ssh_userauth_password(session, username_str, password_str);
    if (auth != SSH_AUTH_SUCCESS) {
        LOGE("SSH password authentication failed: %s", ssh_get_error(session));
        ssh_disconnect(session);
        ssh_free(session);
        session = NULL;
        pthread_mutex_unlock(&session_mutex);
        (*env)->ReleaseStringUTFChars(env, host, host_str);
        (*env)->ReleaseStringUTFChars(env, username, username_str);
        (*env)->ReleaseStringUTFChars(env, password, password_str);
        return JNI_FALSE;
    }

    (*env)->ReleaseStringUTFChars(env, host, host_str);
    (*env)->ReleaseStringUTFChars(env, username, username_str);
    (*env)->ReleaseStringUTFChars(env, password, password_str);

    pthread_mutex_unlock(&session_mutex);
    LOGI("SSH connection established successfully");
    return JNI_TRUE;
}

JNIEXPORT jboolean JNICALL
Java_com_example_sshtunnel_SshTunnelService_connectWithKey(JNIEnv *env, jobject obj, jstring host, jint port, jstring username, jstring privateKeyPath, jstring passphrase) {
    const char *host_str = (*env)->GetStringUTFChars(env, host, 0);
    const char *username_str = (*env)->GetStringUTFChars(env, username, 0);
    const char *privateKey_str = (*env)->GetStringUTFChars(env, privateKeyPath, 0);
    const char *passphrase_str = passphrase ? (*env)->GetStringUTFChars(env, passphrase, 0) : NULL;

    LOGI("Attempting to connect to %s:%d with user %s using key %s", host_str, port, username_str, privateKey_str);

    pthread_mutex_lock(&session_mutex);
#ifndef USE_LIBSSH_MOCK
    if (!g_libssh_initialized) {
        int rc = ssh_init();
        if (rc != SSH_OK) {
            LOGE("ssh_init (lazy) failed with code %d", rc);
            pthread_mutex_unlock(&session_mutex);
            (*env)->ReleaseStringUTFChars(env, host, host_str);
            (*env)->ReleaseStringUTFChars(env, username, username_str);
            (*env)->ReleaseStringUTFChars(env, privateKeyPath, privateKey_str);
            if (passphrase_str) (*env)->ReleaseStringUTFChars(env, passphrase, passphrase_str);
            return JNI_FALSE;
        }
        g_libssh_initialized = 1;
    }
#endif
    session = ssh_new();
    if (session == NULL) {
        LOGE("Failed to create SSH session");
        pthread_mutex_unlock(&session_mutex);
        (*env)->ReleaseStringUTFChars(env, host, host_str);
        (*env)->ReleaseStringUTFChars(env, username, username_str);
        (*env)->ReleaseStringUTFChars(env, privateKeyPath, privateKey_str);
        if (passphrase_str) (*env)->ReleaseStringUTFChars(env, passphrase, passphrase_str);
        return JNI_FALSE;
    }

    int verbosity = SSH_LOG_PROTOCOL;
    ssh_options_set(session, SSH_OPTIONS_LOG_VERBOSITY, &verbosity);
    ssh_options_set(session, SSH_OPTIONS_HOST, host_str);
    ssh_options_set(session, SSH_OPTIONS_PORT, &port);
    ssh_options_set(session, SSH_OPTIONS_USER, username_str);
    ssh_set_blocking(session, 1);

    int connection = ssh_connect(session);
    if (connection != SSH_OK) {
        LOGE("SSH connection failed: %s", ssh_get_error(session));
        ssh_free(session);
        session = NULL;
        pthread_mutex_unlock(&session_mutex);
        (*env)->ReleaseStringUTFChars(env, host, host_str);
        (*env)->ReleaseStringUTFChars(env, username, username_str);
        (*env)->ReleaseStringUTFChars(env, privateKeyPath, privateKey_str);
        if (passphrase_str) (*env)->ReleaseStringUTFChars(env, passphrase, passphrase_str);
        return JNI_FALSE;
    }

    // Try key authentication
    int auth = ssh_userauth_publickey_auto(session, username_str, passphrase_str);
    if (auth != SSH_AUTH_SUCCESS) {
        // Try with specific key file
        ssh_key privkey;
        int import_result = ssh_pki_import_privkey_file(privateKey_str, passphrase_str, NULL, NULL, &privkey);
        if (import_result == SSH_OK) {
            auth = ssh_userauth_publickey(session, username_str, privkey);
            ssh_key_free(privkey);
        }
        
        if (auth != SSH_AUTH_SUCCESS) {
            LOGE("SSH key authentication failed: %s", ssh_get_error(session));
            ssh_disconnect(session);
            ssh_free(session);
            session = NULL;
            pthread_mutex_unlock(&session_mutex);
            (*env)->ReleaseStringUTFChars(env, host, host_str);
            (*env)->ReleaseStringUTFChars(env, username, username_str);
            (*env)->ReleaseStringUTFChars(env, privateKeyPath, privateKey_str);
            if (passphrase_str) (*env)->ReleaseStringUTFChars(env, passphrase, passphrase_str);
            return JNI_FALSE;
        }
    }

    (*env)->ReleaseStringUTFChars(env, host, host_str);
    (*env)->ReleaseStringUTFChars(env, username, username_str);
    (*env)->ReleaseStringUTFChars(env, privateKeyPath, privateKey_str);
    if (passphrase_str) (*env)->ReleaseStringUTFChars(env, passphrase, passphrase_str);

    pthread_mutex_unlock(&session_mutex);
    LOGI("SSH key authentication successful");
    return JNI_TRUE;
}

JNIEXPORT void JNICALL
Java_com_example_sshtunnel_SshTunnelService_disconnect(JNIEnv *env, jobject obj) {
    tunnel_active = 0;
    
    pthread_mutex_lock(&session_mutex);
    if (session != NULL) {
        ssh_disconnect(session);
        ssh_free(session);
        session = NULL;
        LOGI("SSH connection closed");
    }
    pthread_mutex_unlock(&session_mutex);
}

JNIEXPORT jboolean JNICALL
Java_com_example_sshtunnel_SshTunnelService_forwardPort(JNIEnv *env, jobject obj, jint local_port, jstring remote_host, jint remote_port) {
    if (session == NULL) {
        LOGE("SSH session not established");
        return JNI_FALSE;
    }

    // Prevent starting multiple forwarding threads
    if (tunnel_active) {
        LOGI("UDP forwarding already active, skipping duplicate start");
        return JNI_TRUE;
    }

    const char *remote_host_str = (*env)->GetStringUTFChars(env, remote_host, 0);
    
    tunnel_params_t* params = malloc(sizeof(tunnel_params_t));
    if (!params) {
        LOGE("malloc failed for tunnel_params_t");
        (*env)->ReleaseStringUTFChars(env, remote_host, remote_host_str);
        return JNI_FALSE;
    }
    params->local_port = local_port;
    params->remote_port = remote_port;
    params->remote_host = strdup(remote_host_str);
    if (!params->remote_host) {
        LOGE("strdup failed for remote_host");
        free(params);
        (*env)->ReleaseStringUTFChars(env, remote_host, remote_host_str);
        return JNI_FALSE;
    }
    
    (*env)->ReleaseStringUTFChars(env, remote_host, remote_host_str);
    
    tunnel_active = 1;
    
    pthread_t thread;
    if (pthread_create(&thread, NULL, udp_forward_thread, params) != 0) {
        LOGE("Failed to create UDP forwarding thread");
        free(params->remote_host);
        free(params);
        return JNI_FALSE;
    }
    
    pthread_detach(thread);
    LOGI("UDP port forwarding started: %d -> %s:%d", local_port, params->remote_host, remote_port);
    return JNI_TRUE;
}