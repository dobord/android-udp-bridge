#!/usr/bin/env bash
set -euo pipefail
# Start UDP echo on 127.0.0.1:9001 in background
/usr/local/bin/udp_echo 127.0.0.1 9001 &
ECHO_PID=$!
echo "udp_echo started pid=$ECHO_PID"
# Start udp2tcp server
exec /usr/local/bin/udp2tcp_srv /etc/udp2tcp/udp2tcp_srv.yaml
