#!/usr/bin/env bash
#
# scripts/build-wasm.sh
#
# Cross-platform C++ -> WebAssembly compiler wrapper for macOS (Apple Silicon &
# Intel) and Linux. Automatically locates Homebrew LLVM on macOS because Apple's
# default Xcode clang++ does not ship with wasm-ld.

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

# On macOS, prepend Homebrew LLVM (Apple Silicon first, then Intel) so both
# clang++ and wasm-ld are resolved ahead of /usr/bin/clang++.
if [ "$(uname -s)" = "Darwin" ]; then
  if [ -d "/opt/homebrew/opt/llvm/bin" ]; then
    export PATH="/opt/homebrew/opt/llvm/bin:$PATH"
  elif [ -d "/usr/local/opt/llvm/bin" ]; then
    export PATH="/usr/local/opt/llvm/bin:$PATH"
  fi
fi

if ! command -v clang++ >/dev/null 2>&1; then
  echo "ERROR: clang++ not found. On macOS run ./setup-macos.sh (or 'brew install llvm')." >&2
  exit 1
fi

if ! command -v wasm-ld >/dev/null 2>&1; then
  echo "ERROR: wasm-ld not found (Apple Xcode clang++ omits the WebAssembly linker)." >&2
  echo "On macOS, install Homebrew LLVM via ./setup-macos.sh or 'brew install llvm'." >&2
  exit 1
fi

mkdir -p public
clang++ \
  --target=wasm32 \
  -O3 \
  -std=c++17 \
  -nostdlib \
  -fno-exceptions \
  -fno-rtti \
  -Wl,--no-entry \
  -Wl,--export-all \
  -o public/game.wasm \
  src/cpp/game.cpp

echo "Compiled public/game.wasm ($(wc -c < public/game.wasm | tr -d ' ') bytes)"
