#!/bin/bash

# Android SDK and NDK installation script for Linux

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Variables
ANDROID_HOME="/opt/android-sdk"
ANDROID_NDK_VERSION="25.1.8937393"
ANDROID_BUILD_TOOLS_VERSION="34.0.0"
ANDROID_PLATFORM_VERSION="34"

echo "Installing Android SDK and NDK..."

# Create Android SDK directory
sudo mkdir -p $ANDROID_HOME
sudo chown $USER:$USER $ANDROID_HOME

# Download Android Command Line Tools
cd /tmp
wget -q https://dl.google.com/android/repository/commandlinetools-linux-9477386_latest.zip
unzip -q commandlinetools-linux-9477386_latest.zip
mkdir -p $ANDROID_HOME/cmdline-tools
mv cmdline-tools $ANDROID_HOME/cmdline-tools/latest

# Set environment variables
export ANDROID_HOME="/opt/android-sdk"
export PATH=$PATH:$ANDROID_HOME/cmdline-tools/latest/bin:$ANDROID_HOME/platform-tools

# Accept licenses
yes | $ANDROID_HOME/cmdline-tools/latest/bin/sdkmanager --licenses

# Install necessary components
$ANDROID_HOME/cmdline-tools/latest/bin/sdkmanager "platform-tools"
$ANDROID_HOME/cmdline-tools/latest/bin/sdkmanager "platforms;android-$ANDROID_PLATFORM_VERSION"
$ANDROID_HOME/cmdline-tools/latest/bin/sdkmanager "build-tools;$ANDROID_BUILD_TOOLS_VERSION"
$ANDROID_HOME/cmdline-tools/latest/bin/sdkmanager "ndk;$ANDROID_NDK_VERSION"
$ANDROID_HOME/cmdline-tools/latest/bin/sdkmanager "cmake;3.22.1"

# Add to bashrc
echo "export ANDROID_HOME=$ANDROID_HOME" >> ~/.bashrc
echo "export ANDROID_NDK_HOME=\$ANDROID_HOME/ndk/$ANDROID_NDK_VERSION" >> ~/.bashrc
echo "export PATH=\$PATH:\$ANDROID_HOME/cmdline-tools/latest/bin:\$ANDROID_HOME/platform-tools" >> ~/.bashrc

# Create local.properties for the project
mkdir -p "$SCRIPT_DIR/ssh-tunnel-android-app"
cat > "$SCRIPT_DIR/ssh-tunnel-android-app/local.properties" << EOF
sdk.dir=$ANDROID_HOME
ndk.dir=$ANDROID_HOME/ndk/$ANDROID_NDK_VERSION
EOF

echo "Android SDK and NDK installation completed!"
echo "Please run 'source ~/.bashrc' or restart your terminal to update environment variables."
