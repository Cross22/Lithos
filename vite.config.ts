import tailwindcss from '@tailwindcss/vite';
import react from '@vitejs/plugin-react';
import {execFileSync} from 'child_process';
import fs from 'fs';
import path from 'path';
import {defineConfig} from 'vite';

/**
 * Ensures `public/game.wasm` exists (even when gitignored in clean containers)
 * and recompiles only when missing or older than `src/cpp/*` so fresh DWARF
 * debug builds are never overwritten.
 */
function ensureWasmBuilt(): void {
  const wasmPath = path.resolve(__dirname, 'public/game.wasm');
  const cppDir = path.resolve(__dirname, 'src/cpp');
  let latestCppMtime = 0;
  if (fs.existsSync(cppDir)) {
    for (const entry of fs.readdirSync(cppDir)) {
      if (entry.endsWith('.cpp') || entry.endsWith('.h')) {
        const stat = fs.statSync(path.join(cppDir, entry));
        if (stat.mtimeMs > latestCppMtime) {
          latestCppMtime = stat.mtimeMs;
        }
      }
    }
  }

  const wasmExists = fs.existsSync(wasmPath);
  const wasmMtime = wasmExists ? fs.statSync(wasmPath).mtimeMs : 0;
  if (wasmExists && wasmMtime >= latestCppMtime) {
    return;
  }

  try {
    execFileSync('bash', ['scripts/build-wasm.sh'], {
      cwd: __dirname,
      stdio: 'inherit',
      env: {...process.env, WASI_AUTO_INSTALL: '1'},
    });
  } catch (err) {
    console.warn(
      '[ensure-cpp-wasm] Failed to compile public/game.wasm:',
      err instanceof Error ? err.message : String(err)
    );
  }
}

export default defineConfig(() => {
  return {
    plugins: [
      {
        name: 'ensure-cpp-wasm',
        configureServer(): void {
          ensureWasmBuilt();
        },
        buildStart(): void {
          ensureWasmBuilt();
        },
      },
      react(),
      tailwindcss(),
      // Copies src/assets into dist/ so root-relative URLs like
      // "/src/assets/images/..." (e.g. generate_image output) still resolve
      // in production builds. Do not remove.
      {
        name: 'copy-unbundled-src-assets',
        apply: 'build',
        closeBundle(error?: Error): void {
          if (error) {
            return;
          }
          const distDir = path.resolve(__dirname, 'dist');
          const srcAssets = path.resolve(__dirname, 'src/assets');
          const distAssets = path.resolve(distDir, 'src/assets');
          if (fs.existsSync(distDir) && fs.existsSync(srcAssets)) {
            fs.cpSync(srcAssets, distAssets, {recursive: true, force: true});
          }
        },
      },
    ],
    resolve: {
      alias: {
        '@': path.resolve(__dirname, '.'),
      },
    },
    server: {
      // HMR is disabled in AI Studio via DISABLE_HMR env var.
      // Do not modify—file watching is disabled to prevent flickering during agent edits.
      hmr: process.env.DISABLE_HMR !== 'true',
      // Disable file watching when DISABLE_HMR is true to save CPU during agent edits.
      watch: process.env.DISABLE_HMR === 'true' ? null : {},
    },
  };
});
