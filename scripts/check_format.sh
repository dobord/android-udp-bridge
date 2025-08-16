#!/usr/bin/env bash
set -euo pipefail

# Check clang-format across repo using top-level .clang-format.

if ! command -v clang-format >/dev/null 2>&1; then
  echo "clang-format not found in PATH" >&2
  exit 127
fi

mapfile -t FILES < <(git ls-files \
  '*.c' '*.cc' '*.cpp' '*.cxx' '*.h' '*.hpp' | \
  grep -vE '(^|/)build/|(^|/)\.git/|(^|/)openssl_build/|(^|/)third_party/|(^|/)ssh-tunnel-android-app/app/src/main/prebuilt/' || true)

if [ ${#FILES[@]} -eq 0 ]; then
  echo "No C/C++ source files to check."
  exit 0
fi

echo "Checking clang-format on ${#FILES[@]} files..."
if ! clang-format -n -Werror "${FILES[@]}"; then
  echo
  echo "clang-format check failed. To fix locally, run:"
  echo "  scripts/format_all.sh"
  exit 1
fi

echo "clang-format check passed."
