#ifndef LIBSSH_H
#define LIBSSH_H

#include <stdint.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

// Basic types
typedef struct ssh_session_struct* ssh_session;
typedef struct ssh_channel_struct* ssh_channel;

// Return codes
#define SSH_OK 0
#define SSH_ERROR -1
#define SSH_AGAIN -2

// SSH options
enum ssh_options_e {
    SSH_OPTIONS_HOST,
    SSH_OPTIONS_PORT,
    SSH_OPTIONS_USER,
    SSH_OPTIONS_PASSWORD
};

// Authentication results
enum ssh_auth_e {
    SSH_AUTH_SUCCESS = 0,
    SSH_AUTH_DENIED,
    SSH_AUTH_PARTIAL,
    SSH_AUTH_INFO,
    SSH_AUTH_AGAIN,
    SSH_AUTH_ERROR = -1
};

// Function declarations
ssh_session ssh_new(void);
void ssh_free(ssh_session session);
int ssh_connect(ssh_session session);
void ssh_disconnect(ssh_session session);
int ssh_options_set(ssh_session session, enum ssh_options_e type, const void *value);
const char* ssh_get_error(ssh_session session);
int ssh_userauth_password(ssh_session session, const char *username, const char *password);

ssh_channel ssh_channel_new(ssh_session session);
void ssh_channel_free(ssh_channel channel);
int ssh_channel_open_forward(ssh_channel channel, const char *remotehost, int remoteport, const char *sourcehost, int localport);
int ssh_channel_close(ssh_channel channel);
int ssh_channel_write(ssh_channel channel, const void *data, uint32_t len);
int ssh_channel_read(ssh_channel channel, void *dest, uint32_t count, int is_stderr);

#ifdef __cplusplus
}
#endif

#endif /* LIBSSH_H */
