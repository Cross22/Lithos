/**
 * TypeScript <-> C++ WebAssembly Bridge.
 *
 * Loads the compiled freestanding C++ binary (`/game.wasm`), invokes static
 * constructors if present, initializes the 10x20 game engine, routes raw
 * keyboard events into C++, steps the simulation tick, and reads linear memory
 * buffers directly without allocating per-frame copies.
 */

/** Lifecycle states mirrored from C++ constants in `src/cpp/game.cpp`. */
export const GameState = {
  TITLE: 0,
  PLAYING: 1,
  PAUSED: 2,
  GAME_OVER: 3,
} as const;

export type GameStateValue = (typeof GameState)[keyof typeof GameState];

/** Bitmask flags returned by `consume_events()` for audio/visual cues. */
export const WasmEvent = {
  MOVE: 1 << 0,
  ROTATE: 1 << 1,
  LOCK: 1 << 2,
  CLEAR: 1 << 3,
  QUAD: 1 << 4,
  GAME_OVER: 1 << 5,
} as const;

/** Typed ABI surface exported by `src/cpp/game.cpp` via `extern "C"`. */
interface WasmExports {
  memory: WebAssembly.Memory;
  __wasm_call_ctors?: () => void;
  init_game: (seed: number) => void;
  start_game: (seed: number) => void;
  toggle_pause: () => void;
  on_key_event: (keyCode: number, isDown: number) => void;
  tick: (dtMs: number) => void;
  get_render_grid: () => number;
  get_next_grid: () => number;
  get_board_width: () => number;
  get_board_height: () => number;
  get_score: () => number;
  get_high_score: () => number;
  get_lines: () => number;
  get_level: () => number;
  get_elapsed_ms: () => number;
  get_state: () => number;
  consume_events: () => number;
}

/**
 * Immutable-view snapshot of the C++ engine state for a single animation frame.
 * `renderGrid` (200 bytes) and `nextGrid` (16 bytes) are direct `Uint8Array` views
 * into WebAssembly linear memory.
 */
export interface GameSnapshot {
  state: GameStateValue;
  score: number;
  highScore: number;
  lines: number;
  level: number;
  elapsedMs: number;
  width: number;
  height: number;
  renderGrid: Uint8Array;
  nextGrid: Uint8Array;
  events: number;
}

export class CppWasmEngine {
  private exports: WasmExports;
  /** Byte offset in WASM linear memory for the 10x20 composited render grid. */
  private renderPtr: number;
  /** Byte offset in WASM linear memory for the 4x4 next-piece preview grid. */
  private nextPtr: number;
  private boardWidth: number;
  private boardHeight: number;

  private constructor(exports: WasmExports) {
    this.exports = exports;

    // Run C++ global/static constructors emitted by clang/lld when present
    if (typeof this.exports.__wasm_call_ctors === 'function') {
      this.exports.__wasm_call_ctors();
    }

    // Seed the C++ 7-bag PRNG and initialize the 10x20 board to the Title state
    this.exports.init_game(this.randomSeed());
    this.boardWidth = this.exports.get_board_width();
    this.boardHeight = this.exports.get_board_height();
    this.renderPtr = this.exports.get_render_grid();
    this.nextPtr = this.exports.get_next_grid();
  }

  /**
   * Fetches and instantiates the compiled WebAssembly module.
   * Uses streaming instantiation when supported and falls back to ArrayBuffer
   * instantiation if the web server lacks `application/wasm` MIME headers.
   */
  static async load(wasmUrl = '/game.wasm'): Promise<CppWasmEngine> {
    const response = await fetch(wasmUrl);
    if (!response.ok) {
      throw new Error(`Failed to fetch WebAssembly module (${response.status})`);
    }

    let instance: WebAssembly.Instance;
    if (typeof WebAssembly.instantiateStreaming === 'function') {
      try {
        const result = await WebAssembly.instantiateStreaming(response.clone(), {});
        instance = result.instance;
      } catch {
        const buffer = await response.arrayBuffer();
        const result = await WebAssembly.instantiate(buffer, {});
        instance = result.instance;
      }
    } else {
      const buffer = await response.arrayBuffer();
      const result = await WebAssembly.instantiate(buffer, {});
      instance = result.instance;
    }

    return new CppWasmEngine(instance.exports as unknown as WasmExports);
  }

  /** Generates a non-zero 32-bit unsigned integer seed for C++ xorshift32 RNG. */
  private randomSeed(): number {
    if (typeof crypto !== 'undefined' && typeof crypto.getRandomValues === 'function') {
      const arr = new Uint32Array(1);
      crypto.getRandomValues(arr);
      return arr[0] || 0x9e3779b9;
    }
    return (Date.now() ^ (Math.random() * 0xffffffff)) >>> 0;
  }

  /** Resets board, score, elapsed clock, and spawns the first piece in C++. */
  startGame(): void {
    this.exports.start_game(this.randomSeed());
  }

  /** Toggles between PLAYING and PAUSED states inside C++. */
  togglePause(): void {
    this.exports.toggle_pause();
  }

  /**
   * Forwards keyboard press/release events (Arrow keys, WASD, Space, P, R, etc.)
   * directly into the C++ input state machine (including DAS/ARR handling).
   */
  onKeyEvent(keyCode: number, isDown: boolean): void {
    this.exports.on_key_event(keyCode, isDown ? 1 : 0);
  }

  /** Advances C++ gravity, DAS/ARR auto-repeat, line-clear flash, and elapsed clock. */
  tick(dtMs: number): void {
    this.exports.tick(Math.round(dtMs));
  }

  /**
   * Reads scalar HUD metrics and maps zero-copy `Uint8Array` slices over
   * WebAssembly linear memory. Must be refreshed if WASM memory grows.
   */
  getSnapshot(): GameSnapshot {
    const buffer = this.exports.memory.buffer;
    const gridLen = this.boardWidth * this.boardHeight;
    return {
      state: this.exports.get_state() as GameStateValue,
      score: this.exports.get_score(),
      highScore: this.exports.get_high_score(),
      lines: this.exports.get_lines(),
      level: this.exports.get_level(),
      elapsedMs: this.exports.get_elapsed_ms(),
      width: this.boardWidth,
      height: this.boardHeight,
      renderGrid: new Uint8Array(buffer, this.renderPtr, gridLen),
      nextGrid: new Uint8Array(buffer, this.nextPtr, 16),
      events: this.exports.consume_events(),
    };
  }
}
