#!/bin/bash

echo "Testing UDP Forwarder functionality"
echo "==================================="

# Start UDP echo server in background
echo "Starting UDP echo server on localhost:5060..."
./test_udp_echo_server.sh &
ECHO_SERVER_PID=$!

# Wait a moment for server to start
sleep 2

# Test UDP forwarder
echo "Starting UDP forwarder test..."
echo "Press Ctrl+C to stop the test"

# Run UDP forwarder test
timeout 10 ./test_udp_forwarder localhost 5060 || true

# Cleanup
echo "Cleaning up..."
kill $ECHO_SERVER_PID 2>/dev/null || true
wait $ECHO_SERVER_PID 2>/dev/null || true

echo "Test completed!"
