// Minimal mock of libssh API used by ssh_tunnel.c so the app can build without real libssh
#pragma once

typedef struct ssh_session_struct *ssh_session;
typedef struct ssh_channel_struct *ssh_channel;
typedef struct ssh_key_struct *ssh_key;

enum
{
	SSH_OK = 0,
	SSH_ERROR = -1,
	SSH_AUTH_SUCCESS = 0,
	SSH_LOG_PROTOCOL = 2
};

typedef enum
{
	SSH_OPTIONS_LOG_VERBOSITY,
	SSH_OPTIONS_HOST,
	SSH_OPTIONS_PORT,
	SSH_OPTIONS_USER,
	SSH_OPTIONS_CIPHERS_C_S,
	SSH_OPTIONS_CIPHERS_S_C,
	SSH_OPTIONS_KEY_EXCHANGE,
} ssh_options_e;

ssh_session ssh_new(void);
void ssh_free(ssh_session session);
int ssh_connect(ssh_session session);
void ssh_disconnect(ssh_session session);
const char *ssh_get_error(void *err);
int ssh_userauth_password(ssh_session session, const char *username, const char *password);
int ssh_userauth_publickey_auto(ssh_session session, const char *username, const char *passphrase);
int ssh_userauth_publickey(ssh_session session, const char *username, ssh_key privkey);
int ssh_pki_import_privkey_file(const char *filename, const char *passphrase, void *auth_fn, void *auth_data, ssh_key *pkey);
void ssh_key_free(ssh_key key);
int ssh_channel_write(ssh_channel channel, const void *data, unsigned int len);
int ssh_channel_read_timeout(ssh_channel channel, void *dest, unsigned int count, int is_stderr, int timeout_ms);
ssh_channel ssh_channel_new(ssh_session session);
int ssh_channel_open_forward(ssh_channel channel, const char *remote_host, int remote_port, const char *sourcehost, int localport);
void ssh_channel_send_eof(ssh_channel channel);
void ssh_channel_close(ssh_channel channel);
void ssh_channel_free(ssh_channel channel);
int ssh_options_set(ssh_session session, ssh_options_e type, const void *value);
void ssh_set_blocking(ssh_session session, int blocking);
void ssh_channel_set_blocking(ssh_channel channel, int blocking);

// Additional APIs referenced in ssh_tunnel.c but not needed for real I/O in mock
int ssh_get_fd(ssh_session session);		 // Returns a pseudo fd (mocked)
int ssh_channel_is_eof(ssh_channel channel); // Always 0 (not EOF) in mock
int ssh_channel_read_nonblocking(ssh_channel channel, void *dest, unsigned int count, int is_stderr);
