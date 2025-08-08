#include <jni.h>
#include <libssh/libssh.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <android/log.h>

#define LOG_TAG "SSHTunnel"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

static ssh_session session = NULL;
static int tunnel_active = 0;

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
        ssize_t received = recvfrom(local_sock, buffer, sizeof(buffer), 0, 
                                   (struct sockaddr*)&client_addr, &client_len);
        if (received > 0) {
            LOGI("Received UDP packet of %zd bytes", received);
            
            // Forward UDP packet through SSH tunnel
            if (session) {
                ssh_channel channel = ssh_channel_new(session);
                if (channel != NULL) {
                    if (ssh_channel_open_forward(channel, params->remote_host, 
                                               params->remote_port, "localhost", params->local_port) == SSH_OK) {
                        LOGI("SSH channel opened, forwarding data");
                        
                        int written = ssh_channel_write(channel, buffer, received);
                        if (written > 0) {
                            LOGI("Successfully wrote %d bytes to SSH channel", written);
                            
                            // Read response
                            int nbytes = ssh_channel_read(channel, buffer, sizeof(buffer), 0);
                            if (nbytes > 0) {
                                LOGI("Received %d bytes response from SSH channel", nbytes);
                                sendto(local_sock, buffer, nbytes, 0, 
                                      (struct sockaddr*)&client_addr, client_len);
                            }
                        }
                    } else {
                        LOGE("Failed to open SSH channel for forwarding");
                    }
                    ssh_channel_close(channel);
                    ssh_channel_free(channel);
                } else {
                    LOGE("Failed to create SSH channel");
                }
            } else {
                // Fallback: echo data back if no SSH session
                LOGI("No SSH session, echoing data back");
                sendto(local_sock, buffer, received, 0, 
                      (struct sockaddr*)&client_addr, client_len);
            }
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

    session = ssh_new();
    if (session == NULL) {
        LOGE("Failed to create SSH session");
        (*env)->ReleaseStringUTFChars(env, host, host_str);
        (*env)->ReleaseStringUTFChars(env, username, username_str);
        (*env)->ReleaseStringUTFChars(env, password, password_str);
        return JNI_FALSE;
    }

    ssh_options_set(session, SSH_OPTIONS_HOST, host_str);
    ssh_options_set(session, SSH_OPTIONS_PORT, &port);
    ssh_options_set(session, SSH_OPTIONS_USER, username_str);

    int connection = ssh_connect(session);
    if (connection != SSH_OK) {
        LOGE("SSH connection failed: %s", ssh_get_error(session));
        ssh_free(session);
        session = NULL;
        (*env)->ReleaseStringUTFChars(env, host, host_str);
        (*env)->ReleaseStringUTFChars(env, username, username_str);
        (*env)->ReleaseStringUTFChars(env, password, password_str);
        return JNI_FALSE;
    }

    int auth = ssh_userauth_password(session, username_str, password_str);
    if (auth != SSH_AUTH_SUCCESS) {
        LOGE("SSH authentication failed");
        ssh_disconnect(session);
        ssh_free(session);
        session = NULL;
        (*env)->ReleaseStringUTFChars(env, host, host_str);
        (*env)->ReleaseStringUTFChars(env, username, username_str);
        (*env)->ReleaseStringUTFChars(env, password, password_str);
        return JNI_FALSE;
    }

    (*env)->ReleaseStringUTFChars(env, host, host_str);
    (*env)->ReleaseStringUTFChars(env, username, username_str);
    (*env)->ReleaseStringUTFChars(env, password, password_str);

    LOGI("SSH connection established successfully");
    return JNI_TRUE;
}

JNIEXPORT void JNICALL
Java_com_example_sshtunnel_SshTunnelService_disconnect(JNIEnv *env, jobject obj) {
    tunnel_active = 0;
    
    if (session != NULL) {
        ssh_disconnect(session);
        ssh_free(session);
        session = NULL;
        LOGI("SSH connection closed");
    }
}

JNIEXPORT jboolean JNICALL
Java_com_example_sshtunnel_SshTunnelService_forwardPort(JNIEnv *env, jobject obj, jint local_port, jstring remote_host, jint remote_port) {
    if (session == NULL) {
        LOGE("SSH session not established");
        return JNI_FALSE;
    }

    const char *remote_host_str = (*env)->GetStringUTFChars(env, remote_host, 0);
    
    tunnel_params_t* params = malloc(sizeof(tunnel_params_t));
    params->local_port = local_port;
    params->remote_port = remote_port;
    params->remote_host = strdup(remote_host_str);
    
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