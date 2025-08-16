// Minimal mock implementation of libssh used APIs to allow building without real libssh
#include "libssh_mock.h"
#include <stdlib.h>
#include <string.h>

struct ssh_session_struct
{
	char *host;
	int port;
	char *user;
	int blocking;
	char last_error[128];
};

struct ssh_channel_struct
{
	int open;
	int blocking;
};

struct ssh_key_struct
{
	char *filename;
};

static void set_error(struct ssh_session_struct *s, const char *msg)
{
	if (!s)
		return;
	strncpy(s->last_error, msg, sizeof(s->last_error) - 1);
	s->last_error[sizeof(s->last_error) - 1] = '\0';
}

ssh_session ssh_new(void)
{
	struct ssh_session_struct *s = (struct ssh_session_struct *)calloc(1, sizeof(*s));
	if (s)
		s->blocking = 1;
	return s;
}

void ssh_free(ssh_session session)
{
	if (!session)
		return;
	free(session->host);
	free(session->user);
	free(session);
}

int ssh_connect(ssh_session session)
{
	if (!session)
		return SSH_ERROR;
	// Mock success
	return SSH_OK;
}

void ssh_disconnect(ssh_session session)
{
	(void)session;
}

const char *ssh_get_error(void *err)
{
	struct ssh_session_struct *s = (struct ssh_session_struct *)err;
	if (!s)
		return "ssh mock: no error";
	return s->last_error[0] ? s->last_error : "ssh mock: no error";
}

int ssh_userauth_password(ssh_session session, const char *username, const char *password)
{
	(void)password;
	if (!session || !username)
		return SSH_ERROR;
	return SSH_AUTH_SUCCESS;
}

int ssh_userauth_publickey_auto(ssh_session session, const char *username, const char *passphrase)
{
	(void)passphrase;
	if (!session || !username)
		return SSH_ERROR;
	return SSH_AUTH_SUCCESS;
}

int ssh_userauth_publickey(ssh_session session, const char *username, ssh_key privkey)
{
	(void)privkey;
	if (!session || !username)
		return SSH_ERROR;
	return SSH_AUTH_SUCCESS;
}

int ssh_pki_import_privkey_file(const char *filename, const char *passphrase, void *auth_fn, void *auth_data, ssh_key *pkey)
{
	(void)passphrase;
	(void)auth_fn;
	(void)auth_data;
	if (!filename || !pkey)
		return SSH_ERROR;
	struct ssh_key_struct *k = (struct ssh_key_struct *)calloc(1, sizeof(*k));
	if (!k)
		return SSH_ERROR;
	k->filename = strdup(filename);
	*pkey = k;
	return SSH_OK;
}

void ssh_key_free(ssh_key key)
{
	if (!key)
		return;
	free(key->filename);
	free(key);
}

ssh_channel ssh_channel_new(ssh_session session)
{
	(void)session;
	struct ssh_channel_struct *c = (struct ssh_channel_struct *)calloc(1, sizeof(*c));
	if (c)
		c->open = 1;
	return c;
}

int ssh_channel_open_forward(ssh_channel channel, const char *remote_host, int remote_port, const char *sourcehost, int localport)
{
	(void)channel;
	(void)remote_host;
	(void)remote_port;
	(void)sourcehost;
	(void)localport;
	return SSH_OK;
}

int ssh_channel_write(ssh_channel channel, const void *data, unsigned int len)
{
	(void)channel;
	(void)data;
	return (int)len; // pretend all bytes written
}

int ssh_channel_read_timeout(ssh_channel channel, void *dest, unsigned int count, int is_stderr, int timeout_ms)
{
	(void)channel;
	(void)dest;
	(void)count;
	(void)is_stderr;
	(void)timeout_ms;
	return 0; // no data
}

void ssh_channel_send_eof(ssh_channel channel) { (void)channel; }
void ssh_channel_close(ssh_channel channel) { (void)channel; }
void ssh_channel_free(ssh_channel channel)
{
	if (channel)
		free(channel);
}

int ssh_options_set(ssh_session session, ssh_options_e type, const void *value)
{
	if (!session)
		return SSH_ERROR;
	switch (type)
	{
	case SSH_OPTIONS_HOST:
		free(session->host);
		session->host = strdup((const char *)value);
		break;
	case SSH_OPTIONS_PORT:
		session->port = *(const int *)value;
		break;
	case SSH_OPTIONS_USER:
		free(session->user);
		session->user = strdup((const char *)value);
		break;
	case SSH_OPTIONS_LOG_VERBOSITY:
		break;
	case SSH_OPTIONS_CIPHERS_C_S:
	case SSH_OPTIONS_CIPHERS_S_C:
	case SSH_OPTIONS_KEY_EXCHANGE:
		// Mock implementation - just ignore these crypto options
		break;
	}
	return SSH_OK;
}

void ssh_set_blocking(ssh_session session, int blocking)
{
	if (session)
		session->blocking = blocking;
}

void ssh_channel_set_blocking(ssh_channel channel, int blocking)
{
	if (channel)
		channel->blocking = blocking;
}

// Mock: no real socket, return -1 so select() logic in caller ignores session fd
int ssh_get_fd(ssh_session session)
{
	(void)session;
	return -1;
}

int ssh_channel_is_eof(ssh_channel channel)
{
	// In mock we never signal EOF unless channel pointer invalid
	return channel ? 0 : 1;
}

int ssh_channel_read_nonblocking(ssh_channel channel, void *dest, unsigned int count, int is_stderr)
{
	(void)channel;
	(void)dest;
	(void)count;
	(void)is_stderr;
	// No data available in mock
	return 0;
}
