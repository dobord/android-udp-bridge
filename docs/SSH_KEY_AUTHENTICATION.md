# SSH Key Authentication Support (full text)

## Overview

The Android UDP Bridge application adds SSH key authentication support in addition to existing password authentication.

## Authentication Methods

### 1. Password authentication (existing method)
- Simple authentication using username & password
- Suitable for quick testing and simple use cases

### 2. Private key authentication (new method)
- More secure authentication method
- Supports keys with and without passphrase
- Recommended for production use

## User Interface

### Selecting authentication method
- Radio buttons to switch between methods:
	- "Password" – password authentication
	- "Private Key" – key authentication

### Input fields for key authentication
- **Private Key Path**: path to the private key file on the device
- **Key Passphrase**: passphrase for protected key (optional)

## Technical Details

### JNI methods
```java
// Connect with password
public native boolean connectToServer(String host, int port, String username, String password);

// Connect with private key
public native boolean connectWithKey(String host, int port, String username, String privateKeyPath, String passphrase);
```

### Native C functions
```c
// Key authentication
int ssh_userauth_publickey_auto(ssh_session session, const char *username, const char *passphrase);
int ssh_userauth_publickey(ssh_session session, const char *username, const ssh_key privkey);
int ssh_pki_import_privkey_file(const char *filename, const char *passphrase, void *auth_fn, void *auth_data, ssh_key *pkey);
void ssh_key_free(ssh_key key);
```

## Supported Key Formats

In the current stub implementation any key file is accepted (simulation).
Upon integration with real libssh the following will be supported:
- RSA keys
- DSA keys 
- ECDSA keys
- Ed25519 keys
- OpenSSH format
- PEM format

## Usage

### Setup connection with a key
1. Select "Private Key" in Authentication Method
2. Enter private key path (e.g. /sdcard/ssh_keys/id_rsa)
3. Enter passphrase if required
4. Press "Connect"

### Example key paths on Android
- `/sdcard/ssh_keys/id_rsa` - external storage
- `/data/data/com.example.sshtunnel/files/keys/id_rsa` - app internal storage
- `/storage/emulated/0/Download/my_key` - downloads folder

## Security

### Recommendations
- Use passphrase‑protected keys
- Store keys in protected app storage
- Avoid leaving keys in publicly accessible folders
- Rotate keys regularly

### File permissions
The app requires file read permissions to load private keys:
```xml
<uses-permission android:name="android.permission.READ_EXTERNAL_STORAGE" />
```

## Debugging

### Authentication logging
All authentication operations are logged with tag "SSHTunnel":
```
I/SSHTunnel: Attempting to connect to server:22 with user username using key /path/to/key
I/SSHTunnel: SSH key authentication successful
```

### Possible errors
- "SSH key authentication failed" - invalid key or passphrase
- "Failed to create SSH session" - connection/setup issue
- "Please specify private key path" - key path missing

## Future Enhancements

1. **Integration with Android Keystore** - secure key storage
2. **In-app key generation** - create new keys
3. **SSH Agent support** - use system agent
4. **Key file import UI** - file picker for keys
5. **SSH certificates** - certificate-based authentication support
