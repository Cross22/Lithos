/**
 * Root Lithos 10x20 Game Application.
 *
 * Responsibilities:
 *   1. Loads and drives the C++ WebAssembly game engine (`CppWasmEngine`) at 60fps.
 *   2. Uses a ResizeObserver to dynamically compute the largest square `tileSize`
 *      that keeps all 20 rows + the grey marble border visible inside the viewport.
 *   3. Routes keyboard events (Arrow keys, WASD, Space, P/Esc, R, Enter) directly
 *      to the C++ WASM layer via `onKeyEvent`.
 *   4. Renders the right-side readout panel (Score, Best, Elapsed Time, Lines,
 *      Level, Next Tile preview, and control legend) and game state overlays.
 */
import { useCallback, useEffect, useRef, useState } from 'react';
import { Volume2, VolumeX, RotateCcw, Pause, Play } from 'lucide-react';
import { CppWasmEngine, GameSnapshot, GameState, GameStateValue } from './wasm/engine';
import { GameCanvas, NextPieceCanvas } from './components/GameCanvas';
import { soundEngine } from './audio/sound';

const BOARD_COLS = 10;
const BOARD_ROWS = 20;

/**
 * Physical key mapping from `KeyboardEvent.code` to the integer key codes
 * handled inside `src/cpp/game.cpp`. Using `e.code` ensures WASD works
 * consistently regardless of Caps Lock or keyboard layout modifiers.
 */
const CODE_TO_WASM_KEY: Record<string, number> = {
  Enter: 13,
  Escape: 27,
  Space: 32,
  ArrowLeft: 37,
  ArrowUp: 38,
  ArrowRight: 39,
  ArrowDown: 40,
  KeyA: 65,
  KeyD: 68,
  KeyP: 80,
  KeyR: 82,
  KeyS: 83,
  KeyW: 87,
};

/** Formats C++ elapsed milliseconds as `MM:SS.d` with fixed-width padding. */
function formatElapsedTime(ms: number): string {
  const safeMs = Math.max(0, Math.floor(ms));
  const totalSeconds = Math.floor(safeMs / 1000);
  const minutes = Math.floor(totalSeconds / 60);
  const seconds = totalSeconds % 60;
  const tenths = Math.floor((safeMs % 1000) / 100);
  const mm = String(minutes).padStart(2, '0');
  const ss = String(seconds).padStart(2, '0');
  return `${mm}:${ss}.${tenths}`;
}

export default function App() {
  const stageRef = useRef<HTMLDivElement | null>(null);
  const engineRef = useRef<CppWasmEngine | null>(null);

  const [loadError, setLoadError] = useState<string | null>(null);
  const [wasmReady, setWasmReady] = useState(false);
  const [frameVersion, setFrameVersion] = useState(0);
  const [muted, setMuted] = useState(false);

  // HUD state mirrored from C++ WASM getters
  const [gameState, setGameState] = useState<GameStateValue>(GameState.TITLE);
  const [score, setScore] = useState(0);
  const [highScore, setHighScore] = useState(0);
  const [lines, setLines] = useState(0);
  const [level, setLevel] = useState(1);
  const [elapsedMs, setElapsedMs] = useState(0);

  // Latest zero-copy memory views passed down to the Canvas renderers
  const snapshotRef = useRef<GameSnapshot | null>(null);

  // Computed pixel geometry ensuring all 20 rows fit inside the viewport
  const [layout, setLayout] = useState({
    tileSize: 32,
    borderPx: 22,
    hudWidth: 260,
    gapPx: 32,
    compact: false,
  });

  /** Pulls fresh state from C++ WASM, updates React HUD, and fires Web Audio cues. */
  const syncFromSnapshot = useCallback((snap: GameSnapshot) => {
    snapshotRef.current = snap;
    setGameState(snap.state);
    setScore(snap.score);
    setHighScore(snap.highScore);
    setLines(snap.lines);
    setLevel(snap.level);
    setElapsedMs(snap.elapsedMs);
    setFrameVersion((v) => (v + 1) & 0x3fffffff);
    if (snap.events !== 0) {
      soundEngine.playEvents(snap.events);
    }
  }, []);

  // Load and initialize the C++ WebAssembly binary on mount
  useEffect(() => {
    let cancelled = false;
    CppWasmEngine.load('/game.wasm')
      .then((engine) => {
        if (cancelled) return;
        engineRef.current = engine;
        syncFromSnapshot(engine.getSnapshot());
        setWasmReady(true);
      })
      .catch((err) => {
        if (cancelled) return;
        setLoadError(
          err instanceof Error ? err.message : 'Unable to load WebAssembly game engine.'
        );
      });

    return () => {
      cancelled = true;
    };
  }, [syncFromSnapshot]);

  // Dynamically scale square tiles and marble border so all 20 rows remain visible
  useEffect(() => {
    const el = stageRef.current;
    if (!el) return;

    const computeLayout = () => {
      const rect = el.getBoundingClientRect();
      const availW = Math.max(280, rect.width);
      const availH = Math.max(240, rect.height);

      const compact = availW < 640;
      const hudWidth =
        availW >= 960 ? 276 : availW >= 640 ? 236 : Math.max(144, Math.floor(availW * 0.39));
      const gapPx = availW >= 960 ? 36 : availW >= 640 ? 24 : 14;
      const padMargin = availW >= 640 ? 16 : 8;

      const boardAreaW = Math.max(140, availW - hudWidth - gapPx - padMargin);
      const boardAreaH = Math.max(220, availH - padMargin);

      // Marble frame thickness = 0.72 * tileSize on each side:
      //   Total width  = 10 * tileSize + 2 * (0.72 * tileSize) = 11.44 * tileSize
      //   Total height = 20 * tileSize + 2 * (0.72 * tileSize) = 21.44 * tileSize
      let tileSize = Math.floor(Math.min(boardAreaW / 11.44, boardAreaH / 21.44));
      tileSize = Math.max(10, Math.min(44, tileSize));

      // Guard against integer rounding pushing the board 1px outside available bounds
      let borderPx = Math.max(10, Math.round(tileSize * 0.72));
      while (
        tileSize > 10 &&
        (BOARD_ROWS * tileSize + borderPx * 2 > boardAreaH ||
          BOARD_COLS * tileSize + borderPx * 2 > boardAreaW)
      ) {
        tileSize -= 1;
        borderPx = Math.max(10, Math.round(tileSize * 0.72));
      }

      setLayout({ tileSize, borderPx, hudWidth, gapPx, compact });
    };

    computeLayout();
    const observer = new ResizeObserver(computeLayout);
    observer.observe(el);
    window.addEventListener('resize', computeLayout);
    return () => {
      observer.disconnect();
      window.removeEventListener('resize', computeLayout);
    };
  }, []);

  // Main 60fps requestAnimationFrame loop driving C++ tick(dt_ms)
  useEffect(() => {
    if (!wasmReady) return;
    let rafId = 0;
    let lastTime = performance.now();

    const frame = (now: number) => {
      const dt = now - lastTime;
      lastTime = now;

      const engine = engineRef.current;
      if (engine) {
        engine.tick(dt);
        syncFromSnapshot(engine.getSnapshot());
      }
      rafId = requestAnimationFrame(frame);
    };

    rafId = requestAnimationFrame(frame);
    return () => cancelAnimationFrame(rafId);
  }, [wasmReady, syncFromSnapshot]);

  // Global keyboard handler routing Arrow keys, WASD, and shortcuts straight to C++
  useEffect(() => {
    const handleKeyDown = (e: KeyboardEvent) => {
      const wasmKey = CODE_TO_WASM_KEY[e.code];
      if (wasmKey === undefined) return;

      // Prevent browser scrolling or button activation on gameplay keys
      if (
        e.code === 'ArrowUp' ||
        e.code === 'ArrowDown' ||
        e.code === 'ArrowLeft' ||
        e.code === 'ArrowRight' ||
        e.code === 'Space'
      ) {
        e.preventDefault();
      }

      // Ignore OS key repeat events; C++ handles smooth DAS/ARR auto-repeat internally
      if (e.repeat) return;

      soundEngine.unlock();
      const engine = engineRef.current;
      if (!engine) return;

      engine.onKeyEvent(wasmKey, true);
      syncFromSnapshot(engine.getSnapshot());
    };

    const handleKeyUp = (e: KeyboardEvent) => {
      const wasmKey = CODE_TO_WASM_KEY[e.code];
      if (wasmKey === undefined) return;

      const engine = engineRef.current;
      if (!engine) return;

      engine.onKeyEvent(wasmKey, false);
      syncFromSnapshot(engine.getSnapshot());
    };

    // Release held directional inputs if window loses focus so pieces do not stick
    const handleBlur = () => {
      const engine = engineRef.current;
      if (!engine) return;
      [37, 38, 39, 40, 65, 68, 83, 87].forEach((code) => engine.onKeyEvent(code, false));
    };

    window.addEventListener('keydown', handleKeyDown);
    window.addEventListener('keyup', handleKeyUp);
    window.addEventListener('blur', handleBlur);
    return () => {
      window.removeEventListener('keydown', handleKeyDown);
      window.removeEventListener('keyup', handleKeyUp);
      window.removeEventListener('blur', handleBlur);
    };
  }, [syncFromSnapshot]);

  /** Starts or restarts the game in C++ and blurs the button so Space hard-drops. */
  const handleStart = (e?: React.MouseEvent<HTMLButtonElement>) => {
    e?.currentTarget.blur();
    soundEngine.unlock();
    const engine = engineRef.current;
    if (!engine) return;
    engine.startGame();
    syncFromSnapshot(engine.getSnapshot());
  };

  /** Pauses or resumes C++ gravity and elapsed timer. */
  const handleTogglePause = (e?: React.MouseEvent<HTMLButtonElement>) => {
    e?.currentTarget.blur();
    soundEngine.unlock();
    const engine = engineRef.current;
    if (!engine) return;
    engine.togglePause();
    syncFromSnapshot(engine.getSnapshot());
  };

  /** Toggles Web Audio API sound synthesis without affecting C++ engine state. */
  const handleToggleMute = (e: React.MouseEvent<HTMLButtonElement>) => {
    e.currentTarget.blur();
    soundEngine.unlock();
    const nextMuted = !muted;
    soundEngine.setMuted(nextMuted);
    setMuted(nextMuted);
  };

  const boardInnerW = layout.tileSize * BOARD_COLS;
  const boardInnerH = layout.tileSize * BOARD_ROWS;

  return (
    <div className="h-dvh w-screen overflow-hidden bg-[#131518] text-[#F1F3F5] select-none flex flex-col">
      <main
        ref={stageRef}
        className="flex-1 w-full h-full flex items-center justify-center p-2 sm:p-4 lg:p-6 overflow-hidden"
      >
        {loadError ? (
          <div className="max-w-md border border-white/10 bg-[#1B1E23] p-6 rounded-lg text-center">
            <h1 className="font-display text-xl font-semibold text-white mb-2">
              Unable to Initialize Game Engine
            </h1>
            <p className="text-sm text-zinc-400">{loadError}</p>
          </div>
        ) : (
          <div
            className="flex items-stretch justify-center"
            style={{ gap: `${layout.gapPx}px` }}
          >
            {/* Primary Anchor: 10x20 Playing Field framed by Carved Grey Marble */}
            <div className="flex items-center justify-center shrink-0">
              <div
                className="marble-frame rounded-[6px] relative"
                style={{ padding: `${layout.borderPx}px` }}
                aria-label="Grey marble border framing the 10 by 20 playing field"
              >
                <div
                  className="marble-well-recess relative bg-[#0B0D10] overflow-hidden"
                  style={{
                    width: `${boardInnerW}px`,
                    height: `${boardInnerH}px`,
                  }}
                >
                  <GameCanvas
                    tileSize={layout.tileSize}
                    cols={BOARD_COLS}
                    rows={BOARD_ROWS}
                    renderGrid={snapshotRef.current?.renderGrid ?? null}
                    frameVersion={frameVersion}
                  />

                  {/* Loading WebAssembly Engine State */}
                  {!wasmReady && !loadError && (
                    <div className="absolute inset-0 bg-[#0B0D10]/86 flex flex-col items-center justify-center px-5 text-center">
                      <p className="text-xs sm:text-sm text-zinc-300">
                        Loading WebAssembly engine…
                      </p>
                    </div>
                  )}

                  {/* Title Menu Overlay */}
                  {wasmReady && gameState === GameState.TITLE && (
                    <div className="absolute inset-0 bg-[#0B0D10]/86 backdrop-blur-[2px] flex flex-col items-center justify-center px-5 text-center">
                      <h1 className="font-display text-3xl sm:text-4xl font-bold tracking-tight text-white mb-2">
                        Lithos
                      </h1>
                      <p className="text-xs sm:text-sm text-zinc-300 max-w-[230px] leading-relaxed mb-6">
                        Stack falling stone tiles across the 10×20 pit and clear complete rows.
                      </p>
                      <button
                        type="button"
                        onClick={handleStart}
                        className="px-5 py-2.5 rounded-md bg-[#F1F3F5] text-[#131518] text-sm font-semibold hover:bg-white active:scale-[0.98] transition-all cursor-pointer whitespace-nowrap shrink-0 focus-visible:outline-2 focus-visible:outline-offset-2 focus-visible:outline-white"
                      >
                        Start Game
                      </button>
                      <p className="mt-4 text-xs text-zinc-400">
                        Press Enter or Space to begin
                      </p>
                    </div>
                  )}

                  {/* Paused Overlay */}
                  {wasmReady && gameState === GameState.PAUSED && (
                    <div className="absolute inset-0 bg-[#0B0D10]/82 backdrop-blur-[2px] flex flex-col items-center justify-center px-5 text-center">
                      <h2 className="font-display text-2xl sm:text-3xl font-bold tracking-tight text-white mb-2">
                        Paused
                      </h2>
                      <p className="text-xs sm:text-sm text-zinc-400 mb-6">
                        Timer and gravity suspended
                      </p>
                      <div className="flex flex-col gap-2.5 w-full max-w-[180px]">
                        <button
                          type="button"
                          onClick={handleTogglePause}
                          className="w-full px-4 py-2.5 rounded-md bg-[#F1F3F5] text-[#131518] text-sm font-semibold hover:bg-white transition-colors cursor-pointer whitespace-nowrap shrink-0"
                        >
                          Resume
                        </button>
                        <button
                          type="button"
                          onClick={handleStart}
                          className="w-full px-4 py-2 rounded-md border border-white/15 bg-white/5 text-zinc-200 text-sm font-medium hover:bg-white/10 transition-colors cursor-pointer whitespace-nowrap shrink-0"
                        >
                          Restart
                        </button>
                      </div>
                    </div>
                  )}

                  {/* Game Over Overlay */}
                  {wasmReady && gameState === GameState.GAME_OVER && (
                    <div className="absolute inset-0 bg-[#0B0D10]/88 backdrop-blur-[2px] flex flex-col items-center justify-center px-5 text-center">
                      <h2 className="font-display text-2xl sm:text-3xl font-bold tracking-tight text-white mb-1">
                        Game Over
                      </h2>
                      <p className="text-xs text-zinc-400 mb-5">
                        {lines} {lines === 1 ? 'line' : 'lines'} cleared · Level {level}
                      </p>

                      <div className="w-full max-w-[200px] border-t border-b border-white/10 py-3 mb-6 space-y-2">
                        <div className="flex items-baseline justify-between text-sm">
                          <span className="text-zinc-400">Final Score</span>
                          <span className="font-mono-tabular font-semibold text-white">
                            {score.toLocaleString()}
                          </span>
                        </div>
                        <div className="flex items-baseline justify-between text-sm">
                          <span className="text-zinc-400">Elapsed Time</span>
                          <span className="font-mono-tabular font-semibold text-white">
                            {formatElapsedTime(elapsedMs)}
                          </span>
                        </div>
                      </div>

                      <button
                        type="button"
                        onClick={handleStart}
                        className="px-5 py-2.5 rounded-md bg-[#F1F3F5] text-[#131518] text-sm font-semibold hover:bg-white active:scale-[0.98] transition-all cursor-pointer whitespace-nowrap shrink-0 focus-visible:outline-2 focus-visible:outline-offset-2 focus-visible:outline-white"
                      >
                        Play Again
                      </button>
                    </div>
                  )}
                </div>
              </div>
            </div>

            {/* Right-Side HUD: Current Score, Elapsed Time, Next Tile & Controls */}
            <aside
              className="flex flex-col justify-between bg-[#1A1D22] border border-white/[0.08] rounded-md shrink-0 overflow-hidden"
              style={{
                width: `${layout.hudWidth}px`,
                padding: layout.compact ? '12px' : '20px',
              }}
              aria-label="Score, elapsed time, and game controls"
            >
              <div className="space-y-4 sm:space-y-5">
                {/* Panel Header & Audio Mute Toggle */}
                <div className="flex items-center justify-between gap-2 pb-3 border-b border-white/[0.07]">
                  <span className="font-display text-lg sm:text-xl font-bold tracking-tight text-white whitespace-nowrap shrink-0">
                    Lithos
                  </span>
                  <button
                    type="button"
                    onClick={handleToggleMute}
                    aria-label={muted ? 'Unmute sound effects' : 'Mute sound effects'}
                    className="p-2 rounded-md text-zinc-400 hover:text-white hover:bg-white/[0.06] transition-colors cursor-pointer shrink-0 focus-visible:outline-2 focus-visible:outline-white"
                  >
                    {muted ? <VolumeX className="w-4 h-4" /> : <Volume2 className="w-4 h-4" />}
                  </button>
                </div>

                {/* Primary Telemetry: Score & Elapsed Time */}
                <div className="space-y-3 sm:space-y-4">
                  <div>
                    <div className="text-xs font-medium text-zinc-400 mb-0.5">Score</div>
                    <div
                      className={`font-mono-tabular font-bold tracking-tight text-white leading-none ${
                        layout.compact ? 'text-2xl' : 'text-3xl sm:text-4xl'
                      }`}
                    >
                      {score.toLocaleString()}
                    </div>
                    <div className="mt-1.5 text-xs text-zinc-500 font-mono-tabular">
                      Best {highScore.toLocaleString()}
                    </div>
                  </div>

                  <div className="pt-3 border-t border-white/[0.07]">
                    <div className="text-xs font-medium text-zinc-400 mb-0.5">Elapsed Time</div>
                    <div
                      className={`font-mono-tabular font-semibold tracking-tight text-zinc-100 leading-none ${
                        layout.compact ? 'text-xl' : 'text-2xl sm:text-3xl'
                      }`}
                    >
                      {formatElapsedTime(elapsedMs)}
                    </div>
                  </div>
                </div>

                {/* Secondary Progress & Next Piece Preview */}
                <div className="pt-3 border-t border-white/[0.07] space-y-3">
                  <div className="flex items-center justify-between text-xs sm:text-sm text-zinc-300">
                    <span>
                      Lines{' '}
                      <strong className="font-mono-tabular font-semibold text-white ml-1">
                        {lines}
                      </strong>
                    </span>
                    <span className="text-zinc-600" aria-hidden="true">
                      ·
                    </span>
                    <span>
                      Level{' '}
                      <strong className="font-mono-tabular font-semibold text-white ml-1">
                        {level}
                      </strong>
                    </span>
                  </div>

                  {!layout.compact && (
                    <div>
                      <div className="text-xs font-medium text-zinc-400 mb-2">Next Tile</div>
                      <div className="w-full py-2 bg-[#121418] border border-white/[0.06] rounded flex items-center justify-center">
                        <NextPieceCanvas
                          nextGrid={snapshotRef.current?.nextGrid ?? null}
                          frameVersion={frameVersion}
                        />
                      </div>
                    </div>
                  )}
                </div>
              </div>

              {/* Action Buttons & Keyboard Control Legend */}
              <div className="space-y-3 pt-3 border-t border-white/[0.07]">
                <div className="flex items-center gap-2">
                  {gameState === GameState.TITLE || gameState === GameState.GAME_OVER ? (
                    <button
                      type="button"
                      onClick={handleStart}
                      disabled={!wasmReady}
                      className="flex-1 inline-flex items-center justify-center gap-1.5 px-3 py-2 rounded-md bg-[#F1F3F5] text-[#131518] text-xs sm:text-sm font-semibold hover:bg-white transition-colors cursor-pointer whitespace-nowrap shrink-0 disabled:opacity-50"
                    >
                      <Play className="w-3.5 h-3.5" />
                      <span>{gameState === GameState.GAME_OVER ? 'Play Again' : 'Start Game'}</span>
                    </button>
                  ) : (
                    <>
                      <button
                        type="button"
                        onClick={handleTogglePause}
                        className="flex-1 inline-flex items-center justify-center gap-1.5 px-3 py-2 rounded-md bg-white/10 text-white text-xs sm:text-sm font-medium hover:bg-white/15 transition-colors cursor-pointer whitespace-nowrap shrink-0"
                      >
                        {gameState === GameState.PAUSED ? (
                          <>
                            <Play className="w-3.5 h-3.5" />
                            <span>Resume</span>
                          </>
                        ) : (
                          <>
                            <Pause className="w-3.5 h-3.5" />
                            <span>Pause</span>
                          </>
                        )}
                      </button>
                      <button
                        type="button"
                        onClick={handleStart}
                        aria-label="Restart game"
                        title="Restart (R)"
                        className="p-2 rounded-md border border-white/10 text-zinc-300 hover:text-white hover:bg-white/[0.06] transition-colors cursor-pointer shrink-0"
                      >
                        <RotateCcw className="w-4 h-4" />
                      </button>
                    </>
                  )}
                </div>

                {!layout.compact && (
                  <div className="space-y-1.5 text-xs text-zinc-400">
                    <div className="flex items-center justify-between gap-2">
                      <span>Move</span>
                      <span className="font-mono-tabular text-zinc-200">← → / A D</span>
                    </div>
                    <div className="flex items-center justify-between gap-2">
                      <span>Rotate</span>
                      <span className="font-mono-tabular text-zinc-200">↑ / W</span>
                    </div>
                    <div className="flex items-center justify-between gap-2">
                      <span>Soft Drop</span>
                      <span className="font-mono-tabular text-zinc-200">↓ / S</span>
                    </div>
                    <div className="flex items-center justify-between gap-2">
                      <span>Hard Drop</span>
                      <span className="font-mono-tabular text-zinc-200">Space</span>
                    </div>
                    <div className="flex items-center justify-between gap-2">
                      <span>Pause / Reset</span>
                      <span className="font-mono-tabular text-zinc-200">P / R</span>
                    </div>
                  </div>
                )}
              </div>
            </aside>
          </div>
        )}
      </main>
    </div>
  );
}
