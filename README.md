# android-udp-bridge

Android application for secure UDP tunneling over SSH using `udp2tcp` encapsulation.

> Documentation restructured: current files in `docs/`, historical reports in `docs_legacy/`.

## Quick Overview

| Component | Status | Description |
|-----------|--------|-------------|
| SSH (libssh) | Stable | Password / key auth, port forwarding |
| udp2tcp integration | Complete | Default and only data path (UDP over TCP via SSH) |
| Legacy UDP Bridge | Removed | Historical docs retained under `docs_legacy/` |

## Documentation Structure

Current (`docs/`):
- Architecture: [`docs/TECH_SPEC_NEW_ARCHITECTURE.md`](docs/TECH_SPEC_NEW_ARCHITECTURE.md)
- Migration: [`docs/MIGRATION_UDP2TCP.md`](docs/MIGRATION_UDP2TCP.md)
- Implementation / migration plan: [`docs/IMPLEMENTATION_PLAN.md`](docs/IMPLEMENTATION_PLAN.md)
- CI/CD: [`docs/CI_CD_SUMMARY.md`](docs/CI_CD_SUMMARY.md)
- SSH implementation: [`docs/SSH_IMPLEMENTATION.md`](docs/SSH_IMPLEMENTATION.md)
- Advanced SSH library: [`docs/ADVANCED_SSH_LIBRARY.md`](docs/ADVANCED_SSH_LIBRARY.md)
- Key authentication: [`docs/SSH_KEY_AUTHENTICATION.md`](docs/SSH_KEY_AUTHENTICATION.md)

Legacy / reports (`docs_legacy/` examples):
- Protocol / schema: `docs_legacy/UDP_BRIDGE_SCHEMA.md`
- PHASE / TASK reports
- Fixes: `ARCH_FLAGS_FIX*.md`, `ARCHITECTURE_FIX_REPORT.md`

## Migration Status
Migration to `udp2tcp` is complete:
- Legacy listener / client manager / custom protocol removed
- CI enforces presence of `third_party/udp2tcp` submodule
- Tech spec & migration docs updated to udp2tcp-only
- README no longer references feature flags or legacy toggle

`udp2tcp` repository: git@github.com:dobord/udp2tcp.git

## Description

This application establishes an SSH tunnel for UDP traffic on Android. Key capabilities (udp2tcp architecture):

- Connect to SSH server using libssh
- Two authentication methods: password and private key
- UDP port tunneling via TCP encapsulation (`udp2tcp`)
- Secure transfer of UDP data over encrypted SSH forwarding
- Simple UI for configuring the connection

## Technical Notes

### Current (udp2tcp)
- Encapsulation of UDP over a single reliable TCP stream via `udp2tcp` client
- SSH port forwarding (local or dynamic) retained to encrypt transport
- Simplified logic: no custom binary header or client tables in Android layer
- Reduced JNI code (removal of `udp_listener.*`, `udp_bridge_protocol.*`, client manager — phased out)
- Reuse of proven `udp2tcp` code instead of maintaining a custom protocol

### Legacy (custom UDP Bridge protocol) – Historical
Former implementation used a custom binary header (magic, version, crc32) plus client table (registration, ping/pong). Entire stack was removed; see `docs_legacy/` and migration doc for history.

## Usage

### Password authentication
1. Enter SSH server (host, port, username)
2. Select "Password" as Authentication Method
3. Enter password
4. Click "Connect"

### Private key authentication
1. Enter SSH server (host, port, username)
2. Select "Private Key" as Authentication Method
3. Provide path to private key file
4. Enter passphrase if required
5. Click "Connect"

### Configure UDP tunnel (udp2tcp)
1. After successful SSH connection
2. Enter local UDP port (clients will send to it)
3. Specify remote (target) UDP host/port (or use saved config)
4. The app ensures SSH TCP port forwarding to the `udp2tcp` server port
5. Click "Start Forwarding" — udp2tcp client connects to server through SSH tunnel

## Documentation (short index)
See "Documentation Structure" above. New material goes only into `docs/`.

## CI/CD and Automation

Project includes a full CI/CD setup via GitHub Actions:

### 🚀 Automated builds
- **Pull Request Check** - code validation and fast build on PR
- **Main Build** - full build on push to main branches
- **Nightly Build** - daily builds for latest changes
- **Release Build** - automatic release on tag creation

### 📦 Releases
To create a new release:
```bash
# Create and push tag
git tag -a v1.0.0 -m "Release version 1.0.0"
git push origin v1.0.0
```

This triggers automatically:
- Build for all architectures (ARM64, ARMv7, x86, x86_64)
- Signed APK generation
- Release publication on GitHub with changelog
- SHA256 checksum generation

### 🔧 Local workflow testing
```bash
# Local workflow testing (requires act)
./test-workflows.sh validate      # Syntax validation
./test-workflows.sh test-pr       # Pull Request workflow test
./test-workflows.sh setup         # Test environment setup
```

Detailed documentation: [.github/README.md](.github/README.md)

## Build and Test

### Requirements
- Android NDK 25.1.8937393+
- CMake 3.18.1+
- Gradle 8.4+
- Java 21

### Prebuilt libraries (OpenSSL only)

Static prebuilt libraries accelerate local builds.

**Provided:** OpenSSL 3.5.0 + libssh 0.11.2 (arm64-v8a, armeabi-v7a, x86_64, x86)

**Build commands:**
```bash
# Build all architectures (default)
./build_openssl.sh

# Build specific architecture
ANDROID_ABI=arm64-v8a ./build_openssl.sh
ANDROID_ABI=x86_64 ./build_openssl.sh
```

If `ANDROID_ABI` empty or unset all ABIs are built.

### Build commands
```bash
# Build project
cd ssh-tunnel-android-app
./gradlew build

# Run tests
./test_advanced_ssh.sh
```

#### Windows (PowerShell)
```powershell
# 1) (Optional) Ensure Android SDK is available
# If ANDROID_HOME not set, define in local.properties:
#   ssh-tunnel-android-app\local.properties -> sdk.dir=E:\Android\Sdk

# 2) Build and install prebuilt libraries (OpenSSL + libssh)
cd E:\projects\android-udp-bridge
./build_openssl.bat

# 3) Build APK
./build_app.bat

# 4) (Optional) Install APK on device
adb install -r .\ssh-tunnel-android-app\app\build\outputs\apk\debug\app-debug.apk

# 5) (Optional) View logs
adb logcat | Select-String -Pattern "(SSHTunnel|LibSSH_Advanced)"
```

### Install on device
```bash
# Install APK
adb install -r $PWD/ssh-tunnel-android-app/app/build/outputs/apk/debug/app-debug.apk

# Monitor logs
adb logcat | grep -E "(SSHTunnel|LibSSH_Advanced|udp2tcp)"

```

### Tip: always use absolute path for adb install
Во избежание ошибок вида "adb: failed to stat ..." используйте полный путь (через `realpath`):
```bash
ABS_APK="$(realpath ssh-tunnel-android-app/app/build/outputs/apk/debug/app-debug.apk)"
adb install -r "$ABS_APK"
```
Windows (PowerShell):
```powershell
$apk = Resolve-Path .\ssh-tunnel-android-app\app\build\outputs\apk\debug\app-debug.apk
adb install -r $apk
```
Если APK ещё не собран – сначала выполните сборку:
```bash
cd ssh-tunnel-android-app
./gradlew :app:assembleDebug
```

## FAQ (short)
Q: Where are historical PHASE / TASK reports?  
A: In `docs_legacy/`.

Q: Is legacy code still present?  
A: No. Only documentation and historical reports remain in `docs_legacy/`.

Q: Do I need to enable a feature flag for udp2tcp?  
A: No. It is always on and required.

Q: How are stats obtained now?  
A: Java polls a single JNI method exposing aggregate frame/byte counters from the library.

## License
MIT (unless specified otherwise in individual third_party folders).
```