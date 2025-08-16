#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"

# Build udp2tcp server binary if missing
SRV_BIN="../third_party/udp2tcp/build/udp2tcp_srv"
if [[ ! -x "$SRV_BIN" ]]; then
  echo "Building udp2tcp server binary..."
  cmake -S ../third_party/udp2tcp -B ../third_party/udp2tcp/build -D CMAKE_BUILD_TYPE=RelWithDebInfo
  cmake --build ../third_party/udp2tcp/build -j --target udp2tcp_srv
fi

# Ensure docker binary is present
if ! command -v docker >/dev/null 2>&1; then
  echo "docker not found" >&2
  exit 1
fi
if ! command -v docker compose >/dev/null 2>&1; then
  echo "docker compose plugin not found" >&2
  exit 1
fi

# Build image and start stack
export COMPOSE_PROJECT_NAME=udp2tcp_srv_test
cp -f ../third_party/udp2tcp/build/udp2tcp_srv docker/udp2tcp_srv
cp -f ./configs/udp2tcp_srv.yaml docker/udp2tcp_srv.yaml
docker compose -f ./docker-compose.yaml down --remove-orphans || true
docker compose -f ./docker-compose.yaml up -d --build

echo "Waiting for healthcheck..."
for i in {1..30}; do
  if docker inspect --format='{{json .State.Health.Status}}' udp2tcp-srv 2>/dev/null | grep -q '"healthy"'; then
    echo "Service healthy"; break
  fi
  sleep 1
  if [[ $i -eq 30 ]]; then echo "Service did not become healthy" >&2; docker compose -f ./docker-compose.yaml logs --no-color; exit 1; fi
done

echo "Running load: send UDP packets to server and observe stats"
SRV_IP="127.0.0.1"
UDP_PORT=40000
COUNT=100
SIZE=200

echo "Sending $COUNT UDP datagrams of $SIZE bytes..."
for i in $(seq 1 $COUNT); do
  # send UDP packet using bash + /dev/udp
  dd if=/dev/zero bs=$SIZE count=1 2>/dev/null | nc -u -w1 $SRV_IP $UDP_PORT >/dev/null 2>&1 || true
  if (( i % 20 == 0 )); then echo "sent $i"; fi
  sleep 0.02
done

echo "Load done. You can now run desktop-test/udp-bridge-cli with --udp2tcp against this server via your SSH forward."
