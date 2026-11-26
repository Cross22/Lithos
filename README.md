# Lithos 10×20

A 2D tile-based block puzzle game whose core gameplay logic runs in C++17 (using the C++ Standard Library) compiled to WebAssembly (`public/game.wasm`), rendered via a bitmap tile spritesheet inside a carved grey marble border.

## Architecture

- `src/cpp/game.cpp`: C++17 game engine using `<algorithm>`, `<array>`, `<cstdint>`, `<numeric>`, and `<random>`, compiled with `wasi-sdk` (`-mexec-model=reactor`) to WebAssembly. Manages the 10×20 grid, 7-bag tetromino generator (`std::mt19937` + `std::shuffle`), wall-kick rotation, line clearing, scoring, elapsed timer, and Arrow/WASD input with DAS/ARR auto-repeat.
- `src/wasm/engine.ts`: Typed WebAssembly loader and zero-copy linear memory bridge.
- `public/tiles-spritesheet.png`: 512×64 RGBA bitmap spritesheet containing eight 64×64 square tile sprites (alabaster clear flash + 7 mineral stone tile colors).
- `src/components/GameCanvas.tsx`: HiDPI 2D Canvas renderers blitting tile bitmaps from the spritesheet (with `globalAlpha` alpha blending for ghost tiles).
- `src/audio/sound.ts`: Web Audio API synthesized sound effects driven by C++ event flags.
- `src/App.tsx`: Responsive viewport scaler keeping all 20 rows visible at all times, grey marble surround, keyboard event router, and right-side score/elapsed time readout.

## Controls

- **Move Left / Right**: `←` `→` or `A` `D`
- **Rotate**: `↑` or `W`
- **Soft Drop**: `↓` or `S`
- **Hard Drop**: `Space`
- **Pause / Resume**: `P` or `Esc`
- **Restart**: `R`

## Local Development

```bash
npm install
npm run build:wasm   # Requires wasi-sdk
npm run dev
```

## MacBook & VS Code Setup

Run the one-time setup script first (installs `wasi-sdk` into `$HOME/.local/wasi-sdk`, Node.js via Homebrew, and npm dependencies):

```bash
chmod +x setup-macos.sh scripts/build-wasm.sh
./setup-macos.sh
```

Then open the repository in VS Code:
- **Build WASM + Production Bundle**: Press `Cmd+Shift+B` (or run `Build Production Bundle (Vite)`).
- **Build & Run in Browser**: Press `F5` (`Launch Lithos in Chrome (Dev Server)`), which compiles `public/game.wasm`, starts Vite on port `3000`, and opens `http://localhost:3000`.

## Building on GCP `e2-micro`

Run the included build script on a Debian/Ubuntu `e2-micro` instance (handles swap memory guardrails for 1 GB RAM, installs `wasi-sdk`/Node.js if needed, compiles C++ to WebAssembly, lints, and builds `dist/`):

```bash
chmod +x build-e2-micro.sh
./build-e2-micro.sh
```
