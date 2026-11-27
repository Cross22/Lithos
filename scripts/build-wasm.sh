#!/usr/bin/env bash
#
# scripts/build-wasm.sh
#
# Cross-platform C++23 (with C++ Standard Library) -> WebAssembly compiler
# wrapper for macOS (Apple Silicon & Intel) and Linux.
#
# Usage:
#   bash scripts/build-wasm.sh           # Release build (-O3)
#   bash scripts/build-wasm.sh --debug   # DWARF debug build (-O0 -g) for breakpoints
#   WASM_DEBUG=1 bash scripts/build-wasm.sh

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

DEBUG_MODE=0
if [ "${WASM_DEBUG:-0}" = "1" ]; then
  DEBUG_MODE=1
fi
for arg in "$@"; do
  if [ "$arg" = "--debug" ] || [ "$arg" = "-g" ]; then
    DEBUG_MODE=1
  fi
done

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

CXX_FLAGS=(
  -std=c++23
  -mexec-model=reactor
  -fno-exceptions
)

if [ "$DEBUG_MODE" -eq 1 ]; then
  # Unoptimized build with full DWARF debug info for VS Code / Chrome breakpoints
  CXX_FLAGS+=(-O0 -g -gdwarf-4)
  BUILD_LABEL="debug (DWARF)"
else
  CXX_FLAGS+=(-O3)
  BUILD_LABEL="release"
fi

mkdir -p public
"$WASI_SDK_DIR/bin/clang++" \
  "${CXX_FLAGS[@]}" \
  -o public/game.wasm \
  src/cpp/exports.cpp

echo "Compiled public/game.wasm [${BUILD_LABEL}] ($(wc -c < public/game.wasm | tr -d ' ') bytes) via $WASI_SDK_DIR"
