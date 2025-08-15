#!/bin/bash

# Test environment for end-to-end testing of UDP Bridge
# This script sets up the full test environment

set -e

echo "=== Setting up UDP Bridge test environment ==="

# Output colors
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Logging functions
log_info() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

log_warn() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Dependency check
check_dependencies() {
    log_info "Checking dependencies..."
    
    if ! command -v docker &> /dev/null; then
    log_error "Docker not installed"
        exit 1
    fi
    
    if ! command -v docker-compose &> /dev/null; then
    log_error "Docker Compose not installed"
        exit 1
    fi
    
    if ! command -v adb &> /dev/null; then
    log_warn "ADB not found. Android testing will be unavailable"
    fi
    
    log_info "Dependencies verified"
}

# Stop existing containers
cleanup_existing() {
    log_info "Stopping existing containers..."
    cd server-udp-bridge
    docker-compose down --remove-orphans || true
    cd ..
}

# Build server
build_server() {
    log_info "Building UDP Bridge server..."
    cd server-udp-bridge
    
    # Ensure server binary exists
    if [ ! -f "udp-bridge-server" ]; then
    log_info "Building server from sources..."
        make clean && make
    fi
    
    # Build Docker image
    log_info "Building Docker image..."
    docker-compose build
    
    cd ..
    log_info "Server built successfully"
}

# Start server
start_server() {
    log_info "Starting UDP Bridge server..."
    cd server-udp-bridge
    
    # Start in background
    docker-compose up -d
    
    # Wait for readiness
    log_info "Waiting for server readiness..."
    sleep 10
    
    # Check status
    if docker-compose ps | grep -q "Up"; then
    log_info "Server started successfully"
    else
    log_error "Failed to start server"
        docker-compose logs
        exit 1
    fi
    
    cd ..
}

# Check Android SDK
check_android_sdk() {
    log_info "Checking Android SDK..."
    
    if [ -z "$ANDROID_HOME" ]; then
    log_warn "ANDROID_HOME not set"
    # Try to auto-detect SDK
        if [ -d "$HOME/Android/Sdk" ]; then
            export ANDROID_HOME="$HOME/Android/Sdk"
            log_info "Found Android SDK: $ANDROID_HOME"
        else
            log_error "Android SDK not found. Install SDK and set ANDROID_HOME"
            return 1
        fi
    fi
    
    if [ ! -d "$ANDROID_HOME" ]; then
    log_error "Android SDK not found at path: $ANDROID_HOME"
        return 1
    fi
    
    log_info "Android SDK ready"
    return 0
}

# Build Android APK
build_android_apk() {
    log_info "Building Android APK..."
    cd ssh-tunnel-android-app
    
    # Check Gradle wrapper
    if [ ! -f "gradlew" ]; then
    log_error "Gradle wrapper not found"
        cd ..
        return 1
    fi
    
    # Clean and build
    ./gradlew clean
    ./gradlew assembleDebug
    
    # Check result
    APK_PATH="app/build/outputs/apk/debug/app-debug.apk"
    if [ -f "$APK_PATH" ]; then
    log_info "APK built successfully: $APK_PATH"
        cd ..
        return 0
    else
    log_error "Failed to build APK"
        cd ..
        return 1
    fi
}

# Create test data
create_test_data() {
    log_info "Creating test data..."
    
    # Create test directory
    mkdir -p test_results
    
    # Create test configuration
    cat > test_results/test_config.json << EOF
{
    "server": {
        "host": "localhost",
        "ssh_port": 2222,
        "bridge_port": 8080,
        "udp_target_port": 5060
    },
    "client": {
        "local_udp_port": 15060,
        "ssh_user": "sshuser",
        "ssh_password": "sshpassword"
    },
    "tests": {
        "basic_connectivity": true,
        "udp_forwarding": true,
        "multiple_clients": true,
        "stress_test": false
    }
}
EOF

    log_info "Test data created"
}

# Show environment status
show_environment_status() {
    log_info "=== Test environment status ==="
    
    echo "Docker containers:"
    cd server-udp-bridge
    docker-compose ps
    cd ..
    
    echo ""
    echo "Ports:"
    echo "  SSH server: localhost:2222"
    echo "  UDP Bridge: localhost:8080"
    echo "  UDP Target: localhost:5060"
    
    echo ""
    echo "Test credentials:"
    echo "  SSH user: sshuser"
    echo "  SSH password: sshpassword"
    
    if [ -f "ssh-tunnel-android-app/app/build/outputs/apk/debug/app-debug.apk" ]; then
        echo ""
    echo "Android APK: ready"
    else
        echo ""
    echo "Android APK: not built"
    fi
}

# Main function
main() {
    log_info "Starting test environment setup..."
    
    check_dependencies
    cleanup_existing
    build_server
    start_server
    create_test_data
    
    if check_android_sdk; then
        if build_android_apk; then
            log_info "Android APK built successfully"
        else
            log_warn "Failed to build Android APK. End-to-end testing will be limited"
        fi
    else
    log_warn "Android SDK unavailable. Skipping APK build"
    fi
    
    show_environment_status
    
    log_info "=== Test environment is ready! ==="
    log_info "Run tests with: ./run_e2e_tests.sh"
}

# Command-line arguments handling
case "${1:-}" in
    "cleanup")
        cleanup_existing
        ;;
    "server-only")
        check_dependencies
        cleanup_existing
        build_server
        start_server
        show_environment_status
        ;;
    "android-only")
        check_android_sdk && build_android_apk
        ;;
    *)
        main
        ;;
esac
