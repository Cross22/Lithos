#!/usr/bin/env bash
#
# build-e2-micro.sh
#
# Builds Lithos 10x20 (C++ stdlib WebAssembly engine + React/Vite frontend) on a
# low-memory Debian/Ubuntu compute instance such as GCP e2-micro (1 GB RAM).
#
# What this script does:
#   1. Ensures at least ~1.5 GB total virtual memory (adds a 1 GB /swapfile if
#      running on a 1 GB RAM instance without swap so npm/Vite never OOM).
#   2. Installs wasi-sdk 34 into /opt/wasi-sdk (Clang + LLVM libc++ -> WASM)
#      and Node.js 20 LTS if missing.
#   3. Caps V8 heap usage via NODE_OPTIONS="--max-old-space-size=640".
#   4. Compiles src/cpp/game.cpp -> public/game.wasm.
#   5. Installs npm dependencies, runs TypeScript lint, and builds dist/.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

WASI_SDK_TAG="wasi-sdk-34"
WASI_SDK_VER="34.0"
WASI_SDK_DIR="/opt/wasi-sdk"

log() {
  printf '[build-e2-micro] %s\n' "$1"
}

run_priv() {
  if [ "$(id -u)" -eq 0 ]; then
    "$@"
  elif command -v sudo >/dev/null 2>&1; then
    sudo "$@"
  else
    log "ERROR: Root privileges or sudo required to run: $*"
    exit 1
  fi
}

# 1. Guard against Out-Of-Memory kills on 1 GB RAM e2-micro instances
ensure_swap_for_low_ram() {
  local ram_mb swap_mb total_mb
  ram_mb=$(awk '/MemTotal/ {print int($2 / 1024)}' /proc/meminfo)
  swap_mb=$(awk '/SwapTotal/ {print int($2 / 1024)}' /proc/meminfo)
  total_mb=$((ram_mb + swap_mb))

  log "Detected ${ram_mb} MB RAM + ${swap_mb} MB swap (total ${total_mb} MB)."

  if [ "$total_mb" -lt 1500 ] && [ ! -f /swapfile ]; then
    log "Total memory < 1500 MB; creating a 1 GB /swapfile for safe builds..."
    if command -v fallocate >/dev/null 2>&1; then
      run_priv fallocate -l 1G /swapfile || run_priv dd if=/dev/zero of=/swapfile bs=1M count=1024 status=none
    else
      run_priv dd if=/dev/zero of=/swapfile bs=1M count=1024 status=none
    fi
    run_priv chmod 600 /swapfile
    run_priv mkswap /swapfile >/dev/null
    run_priv swapon /swapfile
    log "Swap enabled (/swapfile, 1 GB)."
  fi
}

# 2. Ensure wasi-sdk is installed in /opt/wasi-sdk for C++ stdlib -> WebAssembly
ensure_wasi_sdk() {
  if [ -x "$WASI_SDK_DIR/bin/clang++" ]; then
    log "wasi-sdk found: $("$WASI_SDK_DIR/bin/clang++" --version | head -n 1)"
    return
  fi

  if command -v apt-get >/dev/null 2>&1 && (! command -v curl >/dev/null 2>&1 || ! command -v tar >/dev/null 2>&1); then
    run_priv apt-get update -qq
    run_priv apt-get install -y --no-install-recommends ca-certificates curl tar
  fi

  local arch tarball url
  arch="$(uname -m)"
  if [ "$arch" = "aarch64" ] || [ "$arch" = "arm64" ]; then
    tarball="wasi-sdk-${WASI_SDK_VER}-arm64-linux.tar.gz"
  else
    tarball="wasi-sdk-${WASI_SDK_VER}-x86_64-linux.tar.gz"
  fi
  url="https://github.com/WebAssembly/wasi-sdk/releases/download/${WASI_SDK_TAG}/${tarball}"

  log "Installing ${tarball} into ${WASI_SDK_DIR}..."
  run_priv mkdir -p "$WASI_SDK_DIR"
  if [ "$(id -u)" -eq 0 ]; then
    curl -fsSL "$url" | tar -xz --strip-components=1 -C "$WASI_SDK_DIR"
  else
    curl -fsSL "$url" | sudo tar -xz --strip-components=1 -C "$WASI_SDK_DIR"
  fi
  log "Installed $("$WASI_SDK_DIR/bin/clang++" --version | head -n 1)"
}

# 3. Ensure Node.js >= 18 and npm are installed
ensure_node() {
  local need_node=0
  if ! command -v node >/dev/null 2>&1 || ! command -v npm >/dev/null 2>&1; then
    need_node=1
  else
    local major
    major=$(node -v | sed -E 's/^v([0-9]+).*/\1/')
    if [ "$major" -lt 18 ]; then
      need_node=1
    fi
  fi

  if [ "$need_node" -eq 0 ]; then
    log "Node.js found: $(node -v) / npm $(npm -v)"
    return
  fi

  if ! command -v apt-get >/dev/null 2>&1; then
    log "ERROR: Node.js >= 18 is required. Please install Node.js 20 LTS manually."
    exit 1
  fi

  log "Installing Node.js 20 LTS via NodeSource..."
  run_priv apt-get update -qq
  run_priv apt-get install -y --no-install-recommends ca-certificates curl gnupg
  if [ "$(id -u)" -eq 0 ]; then
    curl -fsSL https://deb.nodesource.com/setup_20.x | bash -
  else
    curl -fsSL https://deb.nodesource.com/setup_20.x | sudo -E bash -
  fi
  run_priv apt-get install -y --no-install-recommends nodejs
  log "Installed Node.js $(node -v) / npm $(npm -v)"
}

ensure_swap_for_low_ram
ensure_wasi_sdk
ensure_node

# Keep Node.js memory footprint safe for 1 GB e2-micro instances
export NODE_OPTIONS="${NODE_OPTIONS:---max-old-space-size=640}"

# 4. Compile C++ stdlib game engine to WebAssembly
log "Compiling src/cpp/game.cpp -> public/game.wasm..."
bash scripts/build-wasm.sh
ls -lh public/game.wasm

# 5. Install Node dependencies conservatively
log "Installing npm dependencies..."
if [ -f package-lock.json ]; then
  npm ci --no-audit --no-fund --maxsockets=4
else
  npm install --no-audit --no-fund --maxsockets=4
fi

# 6. TypeScript check & production Vite build
log "Running TypeScript type check..."
npm run lint

log "Building production bundle into dist/..."
npm run build

log "Build complete!"
log "Preview locally on port 3000 with:"
log "  npx vite preview --host 0.0.0.0 --port 3000"
log "Or serve ./dist with Nginx, Caddy, or any static file server."
