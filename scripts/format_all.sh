#!/usr/bin/env bash
set -euo pipefail

# Format all C/C++ sources in the repo root, excluding nested third_party and build outputs.
# This relies on the top-level .clang-format.

if ! command -v clang-format >/dev/null 2>&1; then
  echo "clang-format not found in PATH" >&2
  exit 127
fi

# Collect files; exclude: third_party/udp2tcp/third_party, build/, .git, openssl_build
mapfile -t FILES < <(git ls-files \
  '*.c' '*.cc' '*.cpp' '*.cxx' '*.h' '*.hpp' | \
  grep -vE '(^|/)build/|(^|/)\.git/|(^|/)openssl_build/|(^|/)third_party/|(^|/)ssh-tunnel-android-app/app/src/main/prebuilt/' || true)

if [ ${#FILES[@]} -eq 0 ]; then
  echo "No C/C++ files found to format."
  exit 0
fi

# Hard safety check: ensure none of the selected files belong to prebuilt tree
if printf '%s\n' "${FILES[@]}" | grep -q '^ssh-tunnel-android-app/app/src/main/prebuilt/'; then
  echo "Safety check failed: prebuilt headers/sources detected in the formatting list." >&2
  printf '%s\n' "${FILES[@]}" | grep '^ssh-tunnel-android-app/app/src/main/prebuilt/' >&2 || true
  exit 2
fi

echo "Formatting ${#FILES[@]} files with clang-format..."
clang-format -i "${FILES[@]}"

echo "Done."
