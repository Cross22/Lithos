#!/usr/bin/env bash
#
# scripts/build-wasm.sh
#
# Cross-platform C++23 (with C++ Standard Library) -> WebAssembly compiler
# wrapper for macOS (Apple Silicon & Intel) and Linux.
#
# Uses wasi-sdk so C++ code can freely use <array>, <algorithm>, <cstdint>,
# <random>, <vector>, etc., and produces a standalone WASI reactor .wasm.

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

# Locate wasi-sdk installation across standard macOS and Linux paths
find_wasi_sdk() {
  local candidates=(
    "${WASI_SDK_PATH:-}"
    "$HOME/.local/wasi-sdk"
    "/opt/wasi-sdk"
    "/opt/homebrew/opt/wasi-sdk"
    "/usr/local/opt/wasi-sdk"
  )
  for dir in "${candidates[@]}"; do
    if [ -n "$dir" ] && [ -x "$dir/bin/clang++" ]; then
      echo "$dir"
      return 0
    fi
  done
  return 1
}

WASI_SDK_DIR="$(find_wasi_sdk || true)"
if [ -z "$WASI_SDK_DIR" ]; then
  if [ "$(uname -s)" = "Darwin" ]; then
    echo "ERROR: wasi-sdk not found. Run ./setup-macos.sh to install wasi-sdk into \$HOME/.local/wasi-sdk." >&2
  else
    echo "ERROR: wasi-sdk not found. Run ./build-e2-micro.sh or install wasi-sdk into /opt/wasi-sdk." >&2
  fi
  exit 1
fi

mkdir -p public
"$WASI_SDK_DIR/bin/clang++" \
  -O3 \
  -std=c++23 \
  -mexec-model=reactor \
  -fno-exceptions \
  -o public/game.wasm \
  src/cpp/exports.cpp

echo "Compiled public/game.wasm ($(wc -c < public/game.wasm | tr -d ' ') bytes) via $WASI_SDK_DIR"
