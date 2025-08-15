#!/bin/bash
# Helper script to collect focused or full logcat logs with timestamps.
# Usage:
#  ./collect_logcat.sh              # capture focused (project related) logs
#  FULL=1 ./collect_logcat.sh       # capture full system log (may be large)
set -euo pipefail
TS=$(date +%Y%m%d_%H%M%S)
mkdir -p logs
if [[ "${FULL:-0}" == "1" ]]; then
  OUT="logs/logcat_full_${TS}.txt"
  echo "[collect_logcat] Clearing log buffer and capturing FULL logcat -> $OUT" >&2
  adb logcat -c || true
  adb logcat -v threadtime | tee "$OUT"
else
  OUT="logs/logcat_focus_${TS}.txt"
  echo "[collect_logcat] Clearing log buffer and capturing focused tags -> $OUT" >&2
  adb logcat -c || true
  # Focus on our native + service + potential channel issues; reduce noise by raising default to Warning
  adb logcat -v threadtime \
    SSHTunnelService:V SSHTunnelApp:V libssh:V udp2tcp:V AndroidRuntime:E \
    *:W | tee "$OUT"
fi
