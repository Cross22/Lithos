#!/usr/bin/env bash
#
# setup-macos.sh
#
# One-time setup script for building and running Lithos 10x20 on a MacBook
# (Apple Silicon or Intel) with VS Code.
#
# Installs / verifies:
#   - Xcode Command Line Tools
#   - Homebrew
#   - LLVM (provides clang++ + wasm-ld for C++ -> WebAssembly)
#   - Node.js (20 LTS or newer)
#   - npm dependencies + initial public/game.wasm build

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT_DIR"

log() {
  printf '[setup-macos] %s\n' "$1"
}

if [ "$(uname -s)" != "Darwin" ]; then
  log "This script is intended for macOS. On Linux/e2-micro, run ./build-e2-micro.sh instead."
  exit 1
fi

# 1. Xcode Command Line Tools
if ! xcode-select -p >/dev/null 2>&1; then
  log "Triggering Xcode Command Line Tools install..."
  xcode-select --install || true
  log "Complete the macOS Command Line Tools prompt, then re-run ./setup-macos.sh."
  exit 1
fi

# 2. Homebrew
if ! command -v brew >/dev/null 2>&1; then
  if [ -x /opt/homebrew/bin/brew ]; then
    eval "$(/opt/homebrew/bin/brew shellenv)"
  elif [ -x /usr/local/bin/brew ]; then
    eval "$(/usr/local/bin/brew shellenv)"
  else
    log "Homebrew is required. Installing Homebrew..."
    /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
    if [ -x /opt/homebrew/bin/brew ]; then
      eval "$(/opt/homebrew/bin/brew shellenv)"
    elif [ -x /usr/local/bin/brew ]; then
      eval "$(/usr/local/bin/brew shellenv)"
    fi
  fi
fi

# 3. Homebrew LLVM (required on macOS because Apple's clang lacks wasm-ld)
if [ ! -x /opt/homebrew/opt/llvm/bin/wasm-ld ] && [ ! -x /usr/local/opt/llvm/bin/wasm-ld ]; then
  log "Installing LLVM (clang++ & wasm-ld) via Homebrew..."
  brew install llvm
else
  log "Homebrew LLVM with wasm-ld is already installed."
fi

# 4. Node.js >= 18
need_node=0
if ! command -v node >/dev/null 2>&1 || ! command -v npm >/dev/null 2>&1; then
  need_node=1
else
  node_major=$(node -v | sed -E 's/^v([0-9]+).*/\1/')
  if [ "$node_major" -lt 18 ]; then
    need_node=1
  fi
fi

if [ "$need_node" -eq 1 ]; then
  log "Installing Node.js via Homebrew..."
  brew install node
else
  log "Node.js found: $(node -v)"
fi

# 5. Install npm packages, compile WASM, and verify TypeScript
log "Installing npm dependencies..."
npm install

log "Compiling C++ WebAssembly engine..."
chmod +x scripts/build-wasm.sh
npm run build:wasm

log "Running TypeScript check..."
npm run lint

log "macOS setup complete! Open this folder in VS Code and press Cmd+Shift+B to build or F5 to launch."
