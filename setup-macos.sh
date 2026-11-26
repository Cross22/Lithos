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
#   - wasi-sdk 34 (Clang + LLVM libc++ + wasi-libc for C++ stdlib -> WASM)
#   - Node.js (20 LTS or newer)
#   - npm dependencies, public/game.wasm build, and TypeScript lint

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT_DIR"

WASI_SDK_TAG="wasi-sdk-34"
WASI_SDK_VER="34.0"
WASI_SDK_DIR="$HOME/.local/wasi-sdk"

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
    log "Installing Homebrew..."
    /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
    if [ -x /opt/homebrew/bin/brew ]; then
      eval "$(/opt/homebrew/bin/brew shellenv)"
    elif [ -x /usr/local/bin/brew ]; then
      eval "$(/usr/local/bin/brew shellenv)"
    fi
  fi
fi

# 3. Install wasi-sdk into $HOME/.local/wasi-sdk for C++ stdlib WebAssembly compilation
if [ ! -x "$WASI_SDK_DIR/bin/clang++" ]; then
  ARCH="$(uname -m)"
  if [ "$ARCH" = "arm64" ]; then
    TARBALL="wasi-sdk-${WASI_SDK_VER}-arm64-macos.tar.gz"
  else
    TARBALL="wasi-sdk-${WASI_SDK_VER}-x86_64-macos.tar.gz"
  fi
  URL="https://github.com/WebAssembly/wasi-sdk/releases/download/${WASI_SDK_TAG}/${TARBALL}"

  log "Downloading ${TARBALL} into ${WASI_SDK_DIR}..."
  mkdir -p "$WASI_SDK_DIR"
  curl -fsSL "$URL" | tar -xz --strip-components=1 -C "$WASI_SDK_DIR"
  log "Installed $("$WASI_SDK_DIR/bin/clang++" --version | head -n 1)"
else
  log "wasi-sdk already installed at $WASI_SDK_DIR"
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

# 5. Install npm dependencies, compile WASM, and verify TypeScript
log "Installing npm dependencies..."
npm install

log "Compiling C++ stdlib WebAssembly engine..."
chmod +x scripts/build-wasm.sh
npm run build:wasm

log "Running TypeScript check..."
npm run lint

log "macOS setup complete! Open this folder in VS Code and press Cmd+Shift+B to build or F5 to launch."
