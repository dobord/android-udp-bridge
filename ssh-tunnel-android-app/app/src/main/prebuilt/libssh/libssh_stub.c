#include <libssh/libssh.h>
#include <stdlib.h>
#include <string.h>
#include <android/log.h>

#define LOG_TAG "LibSSH_Stub"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

// Простая заглушка для структуры сессии
struct ssh_session_struct {
    char* hostname;
    int port;
    char* username;
    char* password;
    int connected;
    char error_msg[256];
};

struct ssh_channel_struct {
    ssh_session session;
    int active;
};

ssh_session ssh_new(void) {
    LOGI("ssh_new() called");
    ssh_session session = (ssh_session)malloc(sizeof(struct ssh_session_struct));
    if (session) {
        memset(session, 0, sizeof(struct ssh_session_struct));
        strcpy(session->error_msg, "No error");
    }
    return session;
}

void ssh_free(ssh_session session) {
    LOGI("ssh_free() called");
    if (session) {
        if (session->hostname) free(session->hostname);
        if (session->username) free(session->username);
        if (session->password) free(session->password);
        free(session);
    }
}

int ssh_connect(ssh_session session) {
    LOGI("ssh_connect() called for %s:%d", session->hostname ? session->hostname : "unknown", session->port);
    if (!session) return SSH_ERROR;
    
    // Симуляция подключения
    session->connected = 1;
    return SSH_OK;
}

void ssh_disconnect(ssh_session session) {
    LOGI("ssh_disconnect() called");
    if (session) {
        session->connected = 0;
    }
}

int ssh_options_set(ssh_session session, enum ssh_options_e type, const void *value) {
    if (!session || !value) return SSH_ERROR;
    
    switch (type) {
        case SSH_OPTIONS_HOST:
            if (session->hostname) free(session->hostname);
            session->hostname = strdup((const char*)value);
            LOGI("Set hostname: %s", session->hostname);
            break;
        case SSH_OPTIONS_PORT:
            session->port = *(const int*)value;
            LOGI("Set port: %d", session->port);
            break;
        case SSH_OPTIONS_USER:
            if (session->username) free(session->username);
            session->username = strdup((const char*)value);
            LOGI("Set username: %s", session->username);
            break;
        case SSH_OPTIONS_PASSWORD:
            if (session->password) free(session->password);
            session->password = strdup((const char*)value);
            LOGI("Set password: [hidden]");
            break;
        default:
            return SSH_ERROR;
    }
    return SSH_OK;
}

const char* ssh_get_error(ssh_session session) {
    return session ? session->error_msg : "Invalid session";
}

int ssh_userauth_password(ssh_session session, const char *username, const char *password) {
    LOGI("ssh_userauth_password() called for user: %s", username ? username : "unknown");
    if (!session) return SSH_AUTH_ERROR;
    
    // Симуляция аутентификации - всегда успешна для тестирования
    return SSH_AUTH_SUCCESS;
}

ssh_channel ssh_channel_new(ssh_session session) {
    LOGI("ssh_channel_new() called");
    if (!session) return NULL;
    
    ssh_channel channel = (ssh_channel)malloc(sizeof(struct ssh_channel_struct));
    if (channel) {
        channel->session = session;
        channel->active = 0;
    }
    return channel;
}

void ssh_channel_free(ssh_channel channel) {
    LOGI("ssh_channel_free() called");
    if (channel) {
        free(channel);
    }
}

int ssh_channel_open_forward(ssh_channel channel, const char *remotehost, int remoteport, const char *sourcehost, int localport) {
    LOGI("ssh_channel_open_forward() called: %s:%d -> %s:%d", 
         sourcehost ? sourcehost : "unknown", localport,
         remotehost ? remotehost : "unknown", remoteport);
    if (!channel) return SSH_ERROR;
    
    // Симуляция открытия форвардинга
    channel->active = 1;
    return SSH_OK;
}

int ssh_channel_close(ssh_channel channel) {
    LOGI("ssh_channel_close() called");
    if (!channel) return SSH_ERROR;
    
    channel->active = 0;
    return SSH_OK;
}

int ssh_channel_write(ssh_channel channel, const void *data, uint32_t len) {
    LOGI("ssh_channel_write() called with %u bytes", len);
    if (!channel || !data) return SSH_ERROR;
    
    // Симуляция записи данных
    return (int)len;
}

int ssh_channel_read(ssh_channel channel, void *dest, uint32_t count, int is_stderr) {
    LOGI("ssh_channel_read() called for %u bytes", count);
    if (!channel || !dest) return SSH_ERROR;
    
    // Симуляция чтения данных (возвращаем эхо)
    return 0; // Нет данных для чтения в заглушке
}
