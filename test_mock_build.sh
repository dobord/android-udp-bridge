#!/bin/bash

echo "🧪 Testing mock build setup..."

cd ssh-tunnel-android-app

# Clean build cache
echo "🧹 Cleaning build cache..."
rm -rf app/.cxx/ app/build/intermediates/cxx/ app/src/main/prebuilt/
./gradlew clean --no-daemon

# Check mock files
echo "🔍 Checking mock files..."
ls -la app/src/main/jni/libssh_mock.*

# Create simple CMakeLists.txt for mock build
echo "🔧 Creating simple CMakeLists.txt..."
cp app/src/main/cpp/CMakeLists.txt app/src/main/cpp/CMakeLists.txt.backup

cat > app/src/main/cpp/CMakeLists.txt << 'EOF'
cmake_minimum_required(VERSION 3.18.1)
project("sshtunnel")

message(STATUS "Building with mock libssh implementation")

# Create mock ssh library
add_library(ssh STATIC ../jni/libssh_mock.c)
target_include_directories(ssh PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../jni)

# Create main library
add_library(ssh_tunnel SHARED 
    ../jni/ssh_tunnel.c
    ../jni/udp_listener.c
    ../jni/tcp_connection_manager.c
    ../jni/client_manager.c
    ../jni/udp_bridge_protocol.c
)
target_compile_definitions(ssh_tunnel PRIVATE USE_LIBSSH_MOCK=1)
target_include_directories(ssh_tunnel PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../jni)

# Find required libraries
find_library(log-lib log)

# Link libraries
target_link_libraries(ssh_tunnel ssh ${log-lib})

# Compile options
target_compile_options(ssh_tunnel PRIVATE 
    -Wall 
    -Wno-unused-parameter 
    -Wno-unused-variable 
    -Wno-unused-function
    -Wno-format-security
    -Wno-incompatible-pointer-types
    -Wno-macro-redefined
)
target_link_options(ssh_tunnel PRIVATE -Wl,-z,max-page-size=16384 -Wl,-z,common-page-size=16384)
EOF

echo "✅ Simple CMakeLists.txt created"

# Try to build
echo "🔨 Attempting build..."
./gradlew assembleDebug --no-daemon --stacktrace

# Check result
if [ -f "app/build/outputs/apk/debug/app-debug.apk" ]; then
    echo "✅ Mock build successful!"
    ls -la app/build/outputs/apk/debug/app-debug.apk
else
    echo "❌ Mock build failed!"
fi

# Restore original
mv app/src/main/cpp/CMakeLists.txt.backup app/src/main/cpp/CMakeLists.txt

echo "🏁 Test completed"
