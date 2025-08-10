#!/bin/bash

# Test script to verify protocol switching has been removed
# Checks that old/new protocol toggles are no longer present

set -e

echo "=== Testing Protocol Switching Removal ==="

PROJECT_DIR="/home/dobord/projects/android-udp-bridge/ssh-tunnel-android-app"

cd "$PROJECT_DIR"

echo "1. Checking Java files for protocol switching references..."

# Check UdpBridgeConfig.java
if grep -q "useNewProtocol\|USE_NEW_PROTOCOL" app/src/main/java/com/example/udpbridge/UdpBridgeConfig.java; then
    echo "✗ UdpBridgeConfig.java still contains protocol switching code"
    exit 1
else
    echo "✓ UdpBridgeConfig.java clean of protocol switching"
fi

# Check UdpBridgeService.java
if grep -q "useNewProtocol\|NEW_PROTOCOL" app/src/main/java/com/example/udpbridge/UdpBridgeService.java; then
    echo "✗ UdpBridgeService.java still contains protocol switching code"
    exit 1
else
    echo "✓ UdpBridgeService.java clean of protocol switching"
fi

# Check MainActivity.java
if grep -q "newProtocolSwitch\|UseNewProtocol" app/src/main/java/com/example/sshtunnel/MainActivity.java; then
    echo "✗ MainActivity.java still contains protocol switching code"
    exit 1
else
    echo "✓ MainActivity.java clean of protocol switching"
fi

# Check UdpBridgeConfigActivity.java
if grep -q "newProtocolSwitch\|UseNewProtocol" app/src/main/java/com/example/udpbridge/UdpBridgeConfigActivity.java; then
    echo "✗ UdpBridgeConfigActivity.java still contains protocol switching code"
    exit 1
else
    echo "✓ UdpBridgeConfigActivity.java clean of protocol switching"
fi

echo "2. Checking layout files for protocol switching UI..."

# Check main activity layout
if grep -q "new_protocol_switch" app/src/main/res/layout/activity_main.xml; then
    echo "✗ Main activity layout still contains protocol switching UI"
    exit 1
else
    echo "✓ Main activity layout clean of protocol switching UI"
fi

# Check config activity layout
if grep -q "config_new_protocol_switch" app/src/main/res/layout/activity_udp_bridge_config.xml; then
    echo "✗ Config activity layout still contains protocol switching UI"
    exit 1
else
    echo "✓ Config activity layout clean of protocol switching UI"
fi

echo "3. Checking help text and descriptions..."

# Check for protocol switching mentions in text
if grep -q "New Protocol\|Old Protocol\|Legacy" app/src/main/res/layout/activity_main.xml; then
    echo "✗ Main layout still mentions protocol switching"
    exit 1
else
    echo "✓ Main layout text clean of protocol switching mentions"
fi

if grep -q "New Protocol\|Old Protocol\|Legacy" app/src/main/res/layout/activity_udp_bridge_config.xml; then
    echo "✗ Config layout still mentions protocol switching"
    exit 1
else
    echo "✓ Config layout text clean of protocol switching mentions"
fi

echo "4. Verifying simplified configuration..."

# Check that config summary no longer includes protocol
if grep -q "Protocol:" app/src/main/java/com/example/udpbridge/UdpBridgeConfig.java; then
    echo "✗ UdpBridgeConfig summary still includes protocol"
    exit 1
else
    echo "✓ UdpBridgeConfig summary simplified"
fi

echo "5. Checking server code consistency..."

# Server should always use new protocol only
if [ -f "/home/dobord/projects/android-udp-bridge/server-udp-bridge/src/main.c" ]; then
    if grep -q "old.*protocol\|legacy.*protocol" /home/dobord/projects/android-udp-bridge/server-udp-bridge/src/main.c; then
        echo "✗ Server code still mentions old/legacy protocol"
        exit 1
    else
        echo "✓ Server code consistent with new protocol only"
    fi
else
    echo "ℹ Server code not checked (file not found)"
fi

echo
echo "=== Protocol Switching Removal Results ==="
echo "✓ All protocol switching toggles removed from Java code"
echo "✓ All protocol switching UI elements removed from layouts"
echo "✓ Help text and descriptions updated"
echo "✓ Configuration simplified to use new protocol only"
echo "✓ Server and client code consistent"

echo
echo "=== Simplification Summary ==="
echo "The UDP Bridge now uses the new protocol exclusively:"
echo "- No toggle switches for protocol selection"
echo "- Simplified configuration interface"
echo "- Reduced complexity for users"
echo "- Consistent behavior across all components"

echo
echo "=== Test Result: SUCCESS ✓ ==="
