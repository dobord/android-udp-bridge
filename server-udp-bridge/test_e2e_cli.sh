#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"

# Params
SSH_HOST=${SSH_HOST:-127.0.0.1}
SSH_PORT=${SSH_PORT:-2222}
SSH_USER=${SSH_USER:-sshuser}
SSH_PASS=${SSH_PASS:-sshpassword}

REMOTE_BRIDGE_HOST=${REMOTE_BRIDGE_HOST:-127.0.0.1}
REMOTE_BRIDGE_PORT=${REMOTE_BRIDGE_PORT:-38080}
# Choose a local TCP listen port for SSH forward; default 47000 but will auto-bump if busy
LOCAL_BRIDGE_PORT=${LOCAL_BRIDGE_PORT:-47000}
# Local UDP listen should NOT clash with docker-published 40000/udp; default to 40100 and auto-bump if busy
LOCAL_UDP_HOST=${LOCAL_UDP_HOST:-127.0.0.1}
LOCAL_UDP_PORT=${LOCAL_UDP_PORT:-40100}
# Remote UDP destination on the server side; the server config forwards to 127.0.0.1:9 (discard)
REMOTE_UDP_HOST=${REMOTE_UDP_HOST:-127.0.0.1}
REMOTE_UDP_PORT=${REMOTE_UDP_PORT:-9}
UDP2TCP_LOG_LEVEL=${UDP2TCP_LOG_LEVEL:-info}

ROOT_DIR="$(cd .. && pwd)"
CLI_BIN="$ROOT_DIR/desktop-test/build/udp-bridge-cli"

if [[ ! -x "$CLI_BIN" ]]; then
  echo "Building desktop-test CLI..."
  cmake -S "$ROOT_DIR/desktop-test" -B "$ROOT_DIR/desktop-test/build"
  cmake --build "$ROOT_DIR/desktop-test/build" -j
fi

find_free_tcp_port() {
  local p=$1; local tries=20
  for i in $(seq 0 $tries); do
    if ! ss -lnt 2>/dev/null | awk '{print $4}' | grep -q ":$p$"; then echo $p; return 0; fi
    p=$((p+1))
  done
  echo "$1"
}

find_free_udp_port() {
  local p=$1; local tries=20
  for i in $(seq 0 $tries); do
    if ! ss -lun 2>/dev/null | awk '{print $5}' | grep -q ":$p$"; then echo $p; return 0; fi
    p=$((p+1))
  done
  echo "$1"
}

LOCAL_BRIDGE_PORT=$(find_free_tcp_port "$LOCAL_BRIDGE_PORT")
LOCAL_UDP_PORT=$(find_free_udp_port "$LOCAL_UDP_PORT")

echo "Starting SSH forward on port $LOCAL_BRIDGE_PORT ..."
"$CLI_BIN" --forward "$REMOTE_BRIDGE_HOST" "$REMOTE_BRIDGE_PORT" "$LOCAL_BRIDGE_PORT" --forward-hold 0 \
  --ssh "$SSH_HOST" "$SSH_USER" "$SSH_PASS" "$SSH_PORT" &
FWD_PID=$!
trap 'kill $FWD_PID 2>/dev/null || true' EXIT

# Wait until the forward port is listening or the process dies
for i in {1..10}; do
  if ! kill -0 $FWD_PID 2>/dev/null; then
    echo "Forward process exited early" >&2
    wait $FWD_PID || true
    exit 1
  fi
  if ss -lnt 2>/dev/null | awk '{print $4}' | grep -q ":$LOCAL_BRIDGE_PORT$"; then
    break
  fi
  sleep 0.5
done
if ! ss -lnt 2>/dev/null | awk '{print $4}' | grep -q ":$LOCAL_BRIDGE_PORT$"; then
  echo "Forward did not start listening on $LOCAL_BRIDGE_PORT" >&2
  exit 1
fi

echo "Running udp2tcp client for 8 seconds..."
"$CLI_BIN" --udp2tcp "$REMOTE_BRIDGE_HOST" "$REMOTE_BRIDGE_PORT" \
  127.0.0.1 "$LOCAL_BRIDGE_PORT" \
  "$LOCAL_UDP_HOST" "$LOCAL_UDP_PORT" \
  "$REMOTE_UDP_HOST" "$REMOTE_UDP_PORT" \
  --udp2tcp-log-level "$UDP2TCP_LOG_LEVEL" \
  --ssh "$SSH_HOST" "$SSH_USER" "$SSH_PASS" "$SSH_PORT" || true

echo "Done. Stopping forward..."
kill $FWD_PID 2>/dev/null || true
wait $FWD_PID 2>/dev/null || true
echo "Complete."
