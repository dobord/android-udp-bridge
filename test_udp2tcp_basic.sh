#!/usr/bin/env bash
set -euo pipefail

# Basic placeholder test for udp2tcp integration
# TODO: implement real device/emulator interaction.

echo "[udp2tcp test] START"
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
APP_DIR="$PROJECT_ROOT/ssh-tunnel-android-app"

if [ ! -d "$APP_DIR" ]; then
  echo "App dir not found" >&2
  exit 1
fi

echo "Building native (NDK) shared library..."
# Rely on gradle build to compile Android.mk via externalNativeBuild if configured
if ! grep -q udp2tcp_client_adapter "$APP_DIR/app/src/main/jni/Android.mk"; then
  echo "Adapter not referenced in Android.mk" >&2
  exit 1
fi

echo "[INFO] (Placeholder) Run gradle assembleDebug if environment ready"
echo "./gradlew :app:assembleDebug  # (manual run)"

echo "[udp2tcp test] DONE (placeholder)"
