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
#include <errno.h>
#include <sys/select.h>
#include <fcntl.h>

#define LOG_TAG "SSHTunnel"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)

static ssh_session session = NULL;
static volatile int tunnel_active = 0;
static pthread_mutex_t session_mutex = PTHREAD_MUTEX_INITIALIZER;

typedef struct {
    int local_port;
    int remote_port;
    char* remote_host;
    ssh_session session_copy;
} tunnel_params_t;

// Structure to handle client connections
typedef struct {
    int client_socket;
    struct sockaddr_in client_addr;
    tunnel_params_t* tunnel_params;
} client_connection_t;

static void* handle_client_connection(void* arg) {
    client_connection_t* conn = (client_connection_t*)arg;
    char buffer[4096];
    
    LOGD("Handling client connection from %s:%d", 
         inet_ntoa(conn->client_addr.sin_addr), 
         ntohs(conn->client_addr.sin_port));
    
    ssh_channel channel = ssh_channel_new(conn->tunnel_params->session_copy);
    if (channel == NULL) {
        LOGE("Failed to create SSH channel");
        close(conn->client_socket);
        free(conn);
        return NULL;
    }
    
    // Open direct-tcpip channel for UDP forwarding
    int rc = ssh_channel_open_forward(channel, 
                                     conn->tunnel_params->remote_host,
                                     conn->tunnel_params->remote_port,
                                     "localhost", 
                                     conn->tunnel_params->local_port);
    
    if (rc != SSH_OK) {
        LOGE("Failed to open SSH forward channel: %s", ssh_get_error(conn->tunnel_params->session_copy));
        ssh_channel_free(channel);
        close(conn->client_socket);
        free(conn);
        return NULL;
    }
    
    LOGD("SSH channel opened successfully");
    
    fd_set read_fds;
    int max_fd;
    struct timeval timeout;
    
    // Set socket to non-blocking
    int flags = fcntl(conn->client_socket, F_GETFL, 0);
    fcntl(conn->client_socket, F_SETFL, flags | O_NONBLOCK);
    
    while (tunnel_active && ssh_channel_is_open(channel)) {
        FD_ZERO(&read_fds);
        FD_SET(conn->client_socket, &read_fds);
        max_fd = conn->client_socket;
        
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;
        
        int activity = select(max_fd + 1, &read_fds, NULL, NULL, &timeout);
        
        if (activity < 0) {
            LOGE("Select error: %s", strerror(errno));
            break;
        }
        
        if (activity == 0) {
            // Timeout, continue to check if tunnel is still active
            continue;
        }
        
        // Check for data from client
        if (FD_ISSET(conn->client_socket, &read_fds)) {
            ssize_t bytes_read = recv(conn->client_socket, buffer, sizeof(buffer), 0);
            if (bytes_read > 0) {
                LOGD("Received %zd bytes from client, forwarding through SSH", bytes_read);
                
                int bytes_written = ssh_channel_write(channel, buffer, bytes_read);
                if (bytes_written != bytes_read) {
                    LOGE("Failed to write all data to SSH channel");
                    break;
                }
                
                // Try to read response
                int bytes_response = ssh_channel_read_timeout(channel, buffer, sizeof(buffer), 0, 1000);
                if (bytes_response > 0) {
                    LOGD("Received %d bytes response, sending back to client", bytes_response);
                    send(conn->client_socket, buffer, bytes_response, 0);
                }
            } else if (bytes_read == 0) {
                LOGD("Client disconnected");
                break;
            } else {
                if (errno != EAGAIN && errno != EWOULDBLOCK) {
                    LOGE("Error reading from client socket: %s", strerror(errno));
                    break;
                }
            }
        }
    }
    
    LOGD("Closing client connection");
    ssh_channel_close(channel);
    ssh_channel_free(channel);
    close(conn->client_socket);
    free(conn);
    return NULL;
}

static void* udp_forward_thread(void* arg) {
    tunnel_params_t* params = (tunnel_params_t*)arg;
    
    // Create TCP socket for better SSH tunnel compatibility
    int listen_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_sock < 0) {
        LOGE("Failed to create listen socket: %s", strerror(errno));
        return NULL;
    }
    
    // Set socket options
    int reuse = 1;
    if (setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
        LOGE("Failed to set SO_REUSEADDR: %s", strerror(errno));
    }
    
    struct sockaddr_in local_addr;
    memset(&local_addr, 0, sizeof(local_addr));
    local_addr.sin_family = AF_INET;
    local_addr.sin_addr.s_addr = INADDR_ANY;
    local_addr.sin_port = htons(params->local_port);
    
    if (bind(listen_sock, (struct sockaddr*)&local_addr, sizeof(local_addr)) < 0) {
        LOGE("Failed to bind socket to port %d: %s", params->local_port, strerror(errno));
        close(listen_sock);
        return NULL;
    }
    
    if (listen(listen_sock, 5) < 0) {
        LOGE("Failed to listen on socket: %s", strerror(errno));
        close(listen_sock);
        return NULL;
    }
    
    LOGI("TCP tunnel listening on port %d", params->local_port);
    
    while (tunnel_active) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(listen_sock, &read_fds);
        
        struct timeval timeout;
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;
        
        int activity = select(listen_sock + 1, &read_fds, NULL, NULL, &timeout);
        
        if (activity < 0) {
            LOGE("Select error on listen socket: %s", strerror(errno));
            break;
        }
        
        if (activity == 0) {
            // Timeout, continue to check if tunnel is still active
            continue;
        }
        
        int client_sock = accept(listen_sock, (struct sockaddr*)&client_addr, &client_len);
        if (client_sock < 0) {
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                LOGE("Failed to accept connection: %s", strerror(errno));
            }
            continue;
        }
        
        LOGI("Accepted connection from %s:%d", 
             inet_ntoa(client_addr.sin_addr), 
             ntohs(client_addr.sin_port));
        
        // Create connection structure for thread
        client_connection_t* conn = malloc(sizeof(client_connection_t));
        if (conn == NULL) {
            LOGE("Failed to allocate memory for client connection");
            close(client_sock);
            continue;
        }
        
        conn->client_socket = client_sock;
        conn->client_addr = client_addr;
        conn->tunnel_params = params;
        
        // Create thread to handle this connection
        pthread_t client_thread;
        if (pthread_create(&client_thread, NULL, handle_client_connection, conn) != 0) {
            LOGE("Failed to create client thread: %s", strerror(errno));
            close(client_sock);
            free(conn);
            continue;
        }
        
        pthread_detach(client_thread);
    }
    
    close(listen_sock);
    LOGI("TCP forwarding thread terminated");
    free(params->remote_host);
    free(params);
    return NULL;
}

JNIEXPORT jboolean JNICALL
Java_com_example_sshtunnel_SshTunnelService_connectToServer(JNIEnv *env, jobject obj, jstring host, jint port, jstring username, jstring password) {
    pthread_mutex_lock(&session_mutex);
    
    // Disconnect existing session if any
    if (session != NULL) {
        ssh_disconnect(session);
        ssh_free(session);
        session = NULL;
    }
    
    const char *host_str = (*env)->GetStringUTFChars(env, host, 0);
    const char *username_str = (*env)->GetStringUTFChars(env, username, 0);
    const char *password_str = (*env)->GetStringUTFChars(env, password, 0);

    LOGI("Connecting to %s:%d with user %s", host_str, port, username_str);

    session = ssh_new();
    if (session == NULL) {
        LOGE("Failed to create SSH session");
        pthread_mutex_unlock(&session_mutex);
        return JNI_FALSE;
    }

    // Set SSH options
    ssh_options_set(session, SSH_OPTIONS_HOST, host_str);
    ssh_options_set(session, SSH_OPTIONS_PORT, &port);
    ssh_options_set(session, SSH_OPTIONS_USER, username_str);
    
    // Set timeout options
    int timeout = 10; // 10 seconds
    ssh_options_set(session, SSH_OPTIONS_TIMEOUT, &timeout);
    
    // Disable strict host key checking for demo purposes
    int strict = 0;
    ssh_options_set(session, SSH_OPTIONS_STRICTHOSTKEYCHECK, &strict);

    int connection = ssh_connect(session);
    if (connection != SSH_OK) {
        LOGE("SSH connection failed: %s", ssh_get_error(session));
        ssh_free(session);
        session = NULL;
        pthread_mutex_unlock(&session_mutex);
        return JNI_FALSE;
    }

    LOGI("SSH connection established, attempting authentication...");

    int auth = ssh_userauth_password(session, username_str, password_str);
    if (auth != SSH_AUTH_SUCCESS) {
        LOGE("SSH authentication failed: %s", ssh_get_error(session));
        ssh_disconnect(session);
        ssh_free(session);
        session = NULL;
        pthread_mutex_unlock(&session_mutex);
        return JNI_FALSE;
    }

    (*env)->ReleaseStringUTFChars(env, host, host_str);
    (*env)->ReleaseStringUTFChars(env, username, username_str);
    (*env)->ReleaseStringUTFChars(env, password, password_str);

    pthread_mutex_unlock(&session_mutex);
    LOGI("SSH connection and authentication successful");
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
    pthread_mutex_lock(&session_mutex);
    
    if (session == NULL) {
        LOGE("SSH session not established");
        pthread_mutex_unlock(&session_mutex);
        return JNI_FALSE;
    }

    const char *remote_host_str = (*env)->GetStringUTFChars(env, remote_host, 0);
    
    tunnel_params_t* params = malloc(sizeof(tunnel_params_t));
    if (params == NULL) {
        LOGE("Failed to allocate memory for tunnel parameters");
        (*env)->ReleaseStringUTFChars(env, remote_host, remote_host_str);
        pthread_mutex_unlock(&session_mutex);
        return JNI_FALSE;
    }
    
    params->local_port = local_port;
    params->remote_port = remote_port;
    params->remote_host = strdup(remote_host_str);
    params->session_copy = session; // Share the session reference
    
    (*env)->ReleaseStringUTFChars(env, remote_host, remote_host_str);
    
    if (params->remote_host == NULL) {
        LOGE("Failed to duplicate remote host string");
        free(params);
        pthread_mutex_unlock(&session_mutex);
        return JNI_FALSE;
    }
    
    tunnel_active = 1;
    
    pthread_t thread;
    if (pthread_create(&thread, NULL, udp_forward_thread, params) != 0) {
        LOGE("Failed to create forwarding thread: %s", strerror(errno));
        free(params->remote_host);
        free(params);
        tunnel_active = 0;
        pthread_mutex_unlock(&session_mutex);
        return JNI_FALSE;
    }
    
    pthread_detach(thread);
    pthread_mutex_unlock(&session_mutex);
    
    LOGI("Port forwarding started: localhost:%d -> %s:%d", local_port, params->remote_host, remote_port);
    return JNI_TRUE;
}