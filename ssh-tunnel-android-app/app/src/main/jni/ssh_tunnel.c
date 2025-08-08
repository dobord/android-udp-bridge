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

#define LOG_TAG "SSHTunnel"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

static ssh_session session = NULL;
static int tunnel_active = 0;
static pthread_mutex_t session_mutex = PTHREAD_MUTEX_INITIALIZER;
#ifndef USE_LIBSSH_MOCK
static volatile int g_libssh_initialized = 0;
#endif

#ifndef USE_LIBSSH_MOCK
// Global libssh log callback (file-scope). Needed to compile with Clang (no nested functions).
static void ssh_android_log_cb(int priority, const char *function, const char *buffer, void *userdata) {
    (void)userdata;
    __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, "libssh[%d] %s: %s", priority,
                        function ? function : "", buffer ? buffer : "");
}
#endif
JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* reserved) {
    // Avoid process kill on SIGPIPE when writing to a closed socket/channel
    signal(SIGPIPE, SIG_IGN);
#ifndef USE_LIBSSH_MOCK
    // Setup libssh logging for better diagnostics
    ssh_set_log_callback(ssh_android_log_cb);
    // Max verbosity to trace functions
    ssh_set_log_level(SSH_LOG_FUNCTIONS);
    // Initialize libssh (and underlying crypto/RNG such as mbedTLS) once per process
    // Set thread callbacks before ssh_init when using threads
    struct ssh_threads_callbacks_struct *cb = ssh_threads_get_default();
    if (cb != NULL) {
        ssh_threads_set_callbacks(cb);
    }
    int rc = ssh_init();
    if (rc == SSH_OK) {
        g_libssh_initialized = 1;
    } else {
        g_libssh_initialized = 0;
        LOGE("ssh_init failed with code %d", rc);
        // We'll try again lazily on first connect call
    }
#endif
    return JNI_VERSION_1_6;
}

#ifndef USE_LIBSSH_MOCK
JNIEXPORT void JNICALL JNI_OnUnload(JavaVM* vm, void* reserved) {
    // Finalize libssh on library unload
    if (g_libssh_initialized) {
        ssh_finalize();
        g_libssh_initialized = 0;
    }
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
    if (!g_libssh_initialized) {
        int rc = ssh_init();
        if (rc != SSH_OK) {
            LOGE("ssh_init (lazy) failed with code %d", rc);
            pthread_mutex_unlock(&session_mutex);
            (*env)->ReleaseStringUTFChars(env, host, host_str);
            (*env)->ReleaseStringUTFChars(env, username, username_str);
            (*env)->ReleaseStringUTFChars(env, password, password_str);
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
        (*env)->ReleaseStringUTFChars(env, password, password_str);
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