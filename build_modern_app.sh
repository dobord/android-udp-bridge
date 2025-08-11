#!/bin/bash

# Build script for modern Amnezia-style UDP Bridge Android app
set -e

echo "🚀 Building UDP Bridge Android App with Modern UI"
echo "================================================="

cd "$(dirname "$0")/ssh-tunnel-android-app"

# Clean previous builds
echo "📦 Cleaning previous builds..."
./gradlew clean

# Check dependencies
echo "📋 Checking dependencies..."
./gradlew dependencies --configuration implementation

# Build the app
echo "🔨 Building debug APK..."
./gradlew assembleDebug

# Check if build was successful
if [ -f "app/build/outputs/apk/debug/app-debug.apk" ]; then
    echo "✅ Build successful!"
    echo "📱 APK location: app/build/outputs/apk/debug/app-debug.apk"
    
    # Get APK size
    APK_SIZE=$(du -h app/build/outputs/apk/debug/app-debug.apk | cut -f1)
    echo "📊 APK size: $APK_SIZE"
    
    # Optional: Install on connected device
    if command -v adb &> /dev/null; then
        echo ""
        read -p "📲 Install on connected device? (y/N): " install_choice
        if [[ $install_choice =~ ^[Yy]$ ]]; then
            echo "📲 Installing APK..."
            adb install -r app/build/outputs/apk/debug/app-debug.apk
            echo "✅ Installation complete!"
        fi
    fi
else
    echo "❌ Build failed!"
    exit 1
fi

echo ""
echo "🎉 Build process completed!"
echo "📝 UI Features:"
echo "   • Dark theme with Amnezia VPN color scheme"
echo "   • Material Design 3 components"
echo "   • Card-based layout"
echo "   • Modern connection button"
echo "   • Improved typography"
echo "   • Advanced settings toggle"
echo "   • Enhanced status indicators"
