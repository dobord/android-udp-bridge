#!/bin/bash

echo "Testing Android app build with new UI..."

cd /home/dobord/projects/android-udp-bridge/ssh-tunnel-android-app

# Clean previous build
./gradlew clean

# Try to build
echo "Building debug APK..."
./gradlew assembleDebug

if [ $? -eq 0 ]; then
    echo "✅ Build successful!"
    echo "APK location: app/build/outputs/apk/debug/app-debug.apk"
else
    echo "❌ Build failed!"
    exit 1
fi
