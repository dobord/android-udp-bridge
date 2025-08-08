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
typedef struct ssh_key_struct* ssh_key;

// Return codes
#define SSH_OK 0
#define SSH_ERROR -1
#define SSH_AGAIN -2

// SSH options
enum ssh_options_e {
    SSH_OPTIONS_HOST,
    SSH_OPTIONS_PORT,
    SSH_OPTIONS_USER,
    SSH_OPTIONS_PASSWORD,
    SSH_OPTIONS_LOG_VERBOSITY,
    SSH_OPTIONS_TIMEOUT,
    SSH_OPTIONS_STRICTHOSTKEYCHECK
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

// Key types
enum ssh_keytypes_e {
    SSH_KEYTYPE_UNKNOWN = 0,
    SSH_KEYTYPE_RSA,
    SSH_KEYTYPE_DSS,
    SSH_KEYTYPE_ECDSA,
    SSH_KEYTYPE_ED25519
};

// Session management
ssh_session ssh_new(void);
void ssh_free(ssh_session session);
int ssh_connect(ssh_session session);
void ssh_disconnect(ssh_session session);
int ssh_options_set(ssh_session session, enum ssh_options_e type, const void *value);
const char* ssh_get_error(ssh_session session);

// Authentication
int ssh_userauth_password(ssh_session session, const char *username, const char *password);
int ssh_userauth_publickey_auto(ssh_session session, const char *username, const char *passphrase);
int ssh_userauth_publickey(ssh_session session, const char *username, const ssh_key privkey);

// Key management
int ssh_pki_import_privkey_file(const char *filename, const char *passphrase, void *auth_fn, void *auth_data, ssh_key *pkey);
void ssh_key_free(ssh_key key);
enum ssh_keytypes_e ssh_key_type(const ssh_key key);
const char *ssh_key_type_to_char(enum ssh_keytypes_e type);

// Channel management
ssh_channel ssh_channel_new(ssh_session session);
void ssh_channel_free(ssh_channel channel);
int ssh_channel_open_forward(ssh_channel channel, const char *remotehost, int remoteport, const char *sourcehost, int localport);
int ssh_channel_close(ssh_channel channel);
int ssh_channel_write(ssh_channel channel, const void *data, uint32_t len);
int ssh_channel_read(ssh_channel channel, void *dest, uint32_t count, int is_stderr);
int ssh_channel_is_eof(ssh_channel channel);

// SFTP support (basic)
typedef struct sftp_session_struct* sftp_session;
sftp_session sftp_new(ssh_session session);
void sftp_free(sftp_session sftp);
int sftp_init(sftp_session sftp);

// Utilities
const char* ssh_version(int req_version);
int ssh_is_connected(ssh_session session);

#ifdef __cplusplus
}
#endif

#endif /* LIBSSH_H */
