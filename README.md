# Lithos 10×20

A 2D tile-based block puzzle game whose core gameplay logic runs in C++ compiled to WebAssembly (`public/game.wasm`), rendered on a responsive HTML5 Canvas inside a carved grey marble border.

## Architecture

- `src/cpp/game.cpp`: Freestanding C++17 game engine compiled to WebAssembly (`--target=wasm32 -nostdlib`). Manages the 10×20 grid, 7-bag tetromino generator, wall-kick rotation, line clearing, scoring, elapsed timer, and Arrow/WASD input with DAS/ARR auto-repeat.
- `src/wasm/engine.ts`: Typed WebAssembly loader and zero-copy linear memory bridge.
- `src/components/GameCanvas.tsx`: HiDPI 2D Canvas renderers for the 10×20 grid and 4×4 next-piece preview.
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
npm run build:wasm   # Requires clang++ and lld
npm run dev
```

## Building on GCP `e2-micro`

Run the included build script on a Debian/Ubuntu `e2-micro` instance (handles swap memory guardrails for 1 GB RAM, installs `clang`/`lld`/Node.js if needed, compiles C++ to WebAssembly, lints, and builds `dist/`):

```bash
chmod +x build-e2-micro.sh
./build-e2-micro.sh
```
