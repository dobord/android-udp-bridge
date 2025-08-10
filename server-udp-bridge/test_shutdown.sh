#!/bin/bash

# Quick test for server shutdown

echo "=== Testing Server Shutdown ==="

# Start server in background
echo "Starting server..."
./udp-bridge-server -p 8082 > test_shutdown.log 2>&1 &
SERVER_PID=$!
echo "Server PID: $SERVER_PID"

sleep 2

# Check if server is running
if ! kill -0 "$SERVER_PID" 2>/dev/null; then
    echo "ERROR: Server failed to start"
    exit 1
fi

echo "Server is running"

# Send SIGTERM
echo "Sending SIGTERM to server..."
kill -TERM "$SERVER_PID"

# Wait for shutdown with timeout
echo "Waiting for graceful shutdown..."
for i in {1..5}; do
    if ! kill -0 "$SERVER_PID" 2>/dev/null; then
        echo "SUCCESS: Server shut down gracefully in $i seconds"
        echo "Server log:"
        cat test_shutdown.log
        rm -f test_shutdown.log
        exit 0
    fi
    echo "Waiting... ($i/5)"
    sleep 1
done

echo "WARNING: Server did not shut down gracefully within 5 seconds"
echo "Force killing server..."
kill -KILL "$SERVER_PID" 2>/dev/null || true

echo "Server log:"
cat test_shutdown.log
rm -f test_shutdown.log

exit 1
