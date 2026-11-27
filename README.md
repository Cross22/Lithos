# Lithos 10×20

A 2D tile-based block puzzle game whose core gameplay logic runs in C++17 (using the C++ Standard Library) compiled to WebAssembly (`public/game.wasm`), rendered via a bitmap tile spritesheet inside a carved grey marble border.

## Architecture

- `src/cpp/exports.cpp`, `src/cpp/game.h`, `src/cpp/helpers.h`: C++23 game engine using `<algorithm>`, `<array>`, `<cstdint>`, `<numeric>`, and `<random>`, compiled with `wasi-sdk` (`-mexec-model=reactor`) to WebAssembly. Manages the 10×20 grid, 7-bag tetromino generator (`std::mt19937` + `ranges::shuffle`), wall-kick rotation, line clearing, scoring, elapsed timer, and Arrow/WASD input with DAS/ARR auto-repeat.
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
npm run build:wasm         # Release WASM (-O3)
npm run build:wasm:debug   # Debug WASM with DWARF symbols (-O0 -g -gdwarf-4)
npm run dev
```

## MacBook & VS Code Setup & DWARF Debugging

Run the one-time setup script first (installs `wasi-sdk` into `$HOME/.local/wasi-sdk`, Node.js via Homebrew, and npm dependencies):

```bash
chmod +x setup-macos.sh scripts/build-wasm.sh
./setup-macos.sh
```

Then open the repository in VS Code:
- **Install Recommended Extensions**: Accept the prompt (or install **WebAssembly DWARF Debugging** `ms-vscode.wasm-dwarf-debugging` and **C/C++** `ms-vscode.cpptools`).
- **Debug with C++ Breakpoints (`F5`)**: Set breakpoints in `src/cpp/exports.cpp`, `src/cpp/game.h`, or `src/cpp/helpers.h`, then select **`Debug Lithos in Chrome (C++ DWARF)`** and press `F5`. This compiles `public/game.wasm` with `-O0 -g -gdwarf-4`, starts Vite on port `3000`, and attaches Chrome with DWARF source/variable mapping.
- **Build Release WASM + Production Bundle**: Press `Cmd+Shift+B` (or run `Build Production Bundle (Vite)`).

## Building on GCP `e2-micro`

Run the included build script on a Debian/Ubuntu `e2-micro` instance (handles swap memory guardrails for 1 GB RAM, installs `wasi-sdk`/Node.js if needed, compiles C++ to WebAssembly, lints, and builds `dist/`):

```bash
chmod +x build-e2-micro.sh
./build-e2-micro.sh
```
