#!/bin/bash

# Build script for SSH Tunnel Android App

set -e

echo "Building SSH Tunnel Android App..."

# Change to project directory
# Resolve script directory and change to project directory relative to it
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR/ssh-tunnel-android-app"

# Check if Android SDK is installed
if [ ! -d "/opt/android-sdk" ]; then
    echo "Android SDK not found. Please run install_android_sdk.sh first."
    exit 1
fi

# Set environment variables
export ANDROID_HOME="/opt/android-sdk"
export ANDROID_NDK_HOME="$ANDROID_HOME/ndk/25.1.8937393"
export PATH=$PATH:$ANDROID_HOME/cmdline-tools/latest/bin:$ANDROID_HOME/platform-tools

# Check if gradlew exists and make it executable
if [ -f "./gradlew" ]; then
    chmod +x ./gradlew
else
    echo "Gradle wrapper not found. Creating it..."
    # If gradle wrapper doesn't exist, we'll need to install gradle
    if ! command -v gradle &> /dev/null; then
        echo "Installing Gradle..."
        sudo apt-get update
        sudo apt-get install -y gradle
    fi
    gradle wrapper
fi

# Clean previous builds
echo "Cleaning previous builds..."
./gradlew clean

# Build debug APK
echo "Building debug APK..."
./gradlew assembleDebug

echo "Build completed!"
echo "APK location: app/build/outputs/apk/debug/app-debug.apk"
