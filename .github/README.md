# GitHub Actions CI/CD Documentation

This document describes the continuous integration and delivery (CI/CD) setup for the SSH Tunnel Android App project.

## 📋 Workflow Overview

### 1. Build and Release (`build-and-release.yml`)
**Triggers:**
- Push to branches `main`, `openssl`
- Tag creation matching `v*`
- Pull requests targeting `main`
- Manual dispatch

**Capabilities:**
- Build dependencies (OpenSSL, libssh) for all architectures
- Build Android APK (debug and release)
- Automated tests
- Create releases when tags are pushed
- Multi-architecture support (ARM64, ARMv7, x86, x86_64)

### 2. Pull Request Check (`pr-check.yml`)
**Triggers:**
- Pull requests to `main`, `openssl`

**Capabilities:**
- Lint and static checks
- Fast build with mock libraries
- Automatic PR comments with results
- Upload lint reports

### 3. Nightly Build (`nightly.yml`)
**Triggers:**
- Daily at 02:00 UTC
- Manual dispatch with forced build option

**Capabilities:**
- Detect changes over last 24h
- Build for multiple architectures and build types
- Generate nightly releases
- Automatic cleanup of stale artifacts
- Status notifications

### 4. Release on Tag (`release.yml`)
**Triggers:**
- Tag creation matching `v*.*.*`

**Capabilities:**
- Tag format validation
- Auto version bump in build.gradle
- APK signing
- Changelog generation from git commits
- Full release creation with documentation
- SHA256 checksum generation

## 🚀 How to Use

### Create a Release

1. **Prepare:**
   ```bash
   git checkout main
   git pull origin main
   ```

2. **Create tag:**
   ```bash
   # Stable release
   git tag -a v1.0.0 -m "Release version 1.0.0"
   
   # Pre-release
   git tag -a v1.0.0-beta -m "Beta release 1.0.0"
   ```

3. **Push tag:**
   ```bash
   git push origin v1.0.0
   ```

4. **Result:**
   - Automatic build for all architectures
   - GitHub Release creation
   - Signed APK files
   - Auto-generated documentation bundle

### Manual Build

1. Go to Actions in GitHub
2. Select "Build and Release"
3. Click "Run workflow"
4. Choose branch and run

### Pull Request Validation

1. Open a PR
2. Automatically runs:
   - Code linting
   - Fast build
   - Comment with summarized results

## 🔧 Configuration

### Environment Variables

| Variable | Description | Used in |
|----------|-------------|---------|
| `ANDROID_HOME` | Path to Android SDK | All workflows |
| `ANDROID_ABI` | Target ABI to build | build-* scripts |
| `GITHUB_TOKEN` | Token for GitHub API | Release creation |

### Secrets (Recommended)

| Secret | Description | Usage |
|--------|-------------|-------|
| `ANDROID_KEYSTORE` | Base64-encoded keystore | APK signing |
| `KEYSTORE_PASSWORD` | Keystore password | APK signing |
| `KEY_ALIAS` | Key alias | APK signing |
| `KEY_PASSWORD` | Key password | APK signing |
| `SLACK_WEBHOOK` | Slack webhook URL | Notifications |

### Adding Secrets

1. Go to Settings → Secrets and variables → Actions
2. Click "New repository secret"
3. Add required secrets

## 📱 Architectures

Supported Android ABIs:

- **arm64-v8a** - 64-bit ARM (modern devices)
- **armeabi-v7a** - 32-bit ARM (legacy devices)
- **x86_64** - 64-bit x86 (emulators / some tablets)
- **x86** - 32-bit x86 (older emulators)

## 🔄 Build Lifecycle

### Regular Development
```
Code Push → PR Check → Review → Merge → Main Build
```

### Release
```
Tag Creation → Validation → Build All Archs → Sign APKs → Create Release
```

### Nightly
```
Schedule → Check Changes → Build → Test → Release → Cleanup
```

## 📊 Monitoring

### Build Status
- GitHub Actions UI shows status
- Artifacts downloadable
- Build logs retained

### Notifications
- Commit statuses
- PR comments
- Email notifications (configurable in GitHub)

## 🛠️ Troubleshooting

### Common Issues

1. **Dependency build failure:**
   - Verify Android SDK availability
   - Ensure scripts are executable

2. **APK signing failure:**
   - Validate keystore secret presence
   - Ensure passwords match secrets

3. **Missing artifacts:**
   - Check completion status of previous jobs
   - Validate artifact paths

### Debugging

1. **Enable debug mode:**
   ```yaml
   - name: Enable debug
     run: echo "ACTIONS_STEP_DEBUG=true" >> $GITHUB_ENV
   ```

2. **Add extra logging:**
   ```yaml
   - name: Debug info
     run: |
       echo "Current directory: $(pwd)"
       echo "Files: $(ls -la)"
       echo "Environment: $(env | sort)"
   ```

## 📈 Optimization

### Caching
- Gradle build cache
- Android SDK cache
- Dependencies cached between runs

### Parallelism
- Different architecture jobs run in parallel
- Independent jobs overlap

### Resources
- Standard GitHub runner (2 CPU, 7GB RAM)
- Self-hosted runners possible for larger builds

## 🔐 Security

### APK Signing
- Temporary/demo keystore used
- Use repository secrets in production

### Secrets
- Never commit secrets
- Use GitHub Secrets for sensitive data

### Validation
- Automatic APK integrity checks
- SHA256 checksums for all releases

## 📚 Additional Resources

- [GitHub Actions Documentation](https://docs.github.com/en/actions)
- [Android CI/CD Best Practices](https://developer.android.com/studio/publish/app-signing)
- [Gradle Build Cache](https://docs.gradle.org/current/userguide/build_cache.html)
