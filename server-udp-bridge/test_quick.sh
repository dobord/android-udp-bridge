#!/bin/bash

# Quick test for improved server - sends messages without waiting for responses

echo "=== Quick Server Test ==="

echo "Test 1: Multiple quick connections"
for i in {1..3}; do
    echo "Connection $i:"
    (echo "test data $i" | timeout 1 nc localhost 8080) &
    sleep 0.1
done
wait

echo -e "\nTest 2: Protocol messages"
# Send a simple protocol message
echo -ne "\x55\x44\x50\x42\x01\x02\x00\x00\x39\x30\x00\x00\x00\x00\x00\x00\x3c\x00\x00\x00" | timeout 1 nc localhost 8080 &
sleep 0.5

echo -e "\nTest 3: Multiple simultaneous connections"
for i in {1..5}; do
    (echo "concurrent test $i" | timeout 2 nc localhost 8080) &
done
wait

echo -e "\nTesting complete - check server output for handling details"
