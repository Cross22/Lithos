/**
 * Game Audio Engine.
 *
 * Manages:
 *   1. Looping background music (`/assets/aistudio/tetrisViolin_5ce25aed.mp3`)
 *      that plays whenever the game is running (`GameState.PLAYING`), pauses
 *      when paused (`GameState.PAUSED`), and resets when ended or restarted.
 *   2. Line-clear shatter sound (`/assets/aistudio/glassBreaking_2b91e845.mp3`)
 *      played whenever lines are cleared on the current frame (`CLEAR` / `QUAD`).
 *   3. Subtle Web Audio API synthesized feedback for moves, rotations, locks,
 *      and game over, all respecting the user mute toggle and browser autoplay unlock.
 */
import { GameState, GameStateValue, WasmEvent } from '../wasm/engine';

const MUSIC_URL = '/assets/aistudio/tetrisViolin_5ce25aed.mp3';
const GLASS_BREAKING_URL = '/assets/aistudio/glassBreaking_2b91e845.mp3';

class SoundEngine {
  private ctx: AudioContext | null = null;
  private muted = false;

  private musicAudio: HTMLAudioElement | null = null;
  private glassBuffer: AudioBuffer | null = null;
  private glassLoading = false;
  private glassFallback: HTMLAudioElement | null = null;
  private lastMusicState: GameStateValue | null = null;

  /** Enables or mutes all music and sound effects. */
  setMuted(muted: boolean): void {
    this.muted = muted;
    if (this.musicAudio) {
      this.musicAudio.muted = muted;
    }
    if (this.glassFallback) {
      this.glassFallback.muted = muted;
    }
  }

  isMuted(): boolean {
    return this.muted;
  }

  /** Lazily constructs the looping HTMLAudioElement for the violin soundtrack. */
  private ensureMusicElement(): HTMLAudioElement | null {
    if (typeof Audio === 'undefined') return null;
    if (!this.musicAudio) {
      const audio = new Audio(MUSIC_URL);
      audio.loop = true;
      audio.preload = 'auto';
      audio.volume = 0.45;
      audio.muted = this.muted;
      this.musicAudio = audio;
    }
    return this.musicAudio;
  }

  /** Lazily creates a fallback HTMLAudioElement for the glass-breaking SFX. */
  private ensureGlassFallback(): HTMLAudioElement | null {
    if (typeof Audio === 'undefined') return null;
    if (!this.glassFallback) {
      const audio = new Audio(GLASS_BREAKING_URL);
      audio.preload = 'auto';
      audio.volume = 0.85;
      audio.muted = this.muted;
      this.glassFallback = audio;
    }
    return this.glassFallback;
  }

  /** Fetches and decodes the glass-breaking MP3 into an AudioBuffer for zero-lag playback. */
  private preloadGlassBuffer(): void {
    if (this.glassBuffer || this.glassLoading || !this.ctx) return;
    this.glassLoading = true;
    const ctx = this.ctx;
    fetch(GLASS_BREAKING_URL)
      .then((res) => (res.ok ? res.arrayBuffer() : Promise.reject(new Error('Audio fetch failed'))))
      .then((buf) => ctx.decodeAudioData(buf))
      .then((decoded) => {
        this.glassBuffer = decoded;
      })
      .catch(() => {
        this.glassLoading = false;
      });
  }

  /**
   * Lazily initializes and resumes the AudioContext on the first user gesture
   * (keyboard press or button click) so browsers allow audio playback.
   */
  unlock(): void {
    if (typeof window === 'undefined') return;
    if (!this.ctx) {
      const AudioCtx =
        window.AudioContext ||
        (window as unknown as { webkitAudioContext: typeof AudioContext }).webkitAudioContext;
      if (AudioCtx) {
        this.ctx = new AudioCtx();
      }
    }
    if (this.ctx && this.ctx.state === 'suspended') {
      this.ctx.resume().catch(() => {});
    }
    this.ensureMusicElement();
    this.ensureGlassFallback();
    this.preloadGlassBuffer();
  }

  /**
   * Synchronizes the background violin track with the C++ game state.
   * Plays while `PLAYING`, pauses in place while `PAUSED`, and stops/rewinds on
   * `TITLE` or `GAME_OVER`.
   */
  syncGameState(state: GameStateValue, forceRestart = false): void {
    const music = this.ensureMusicElement();
    if (!music) return;
    music.muted = this.muted;

    if (state === GameState.PLAYING) {
      const startingFresh =
        forceRestart ||
        this.lastMusicState === GameState.TITLE ||
        this.lastMusicState === GameState.GAME_OVER;

      if (startingFresh) {
        music.currentTime = 0;
      }
      if (music.paused || startingFresh) {
        music.play().catch(() => {});
      }
    } else if (state === GameState.PAUSED) {
      if (!music.paused) {
        music.pause();
      }
    } else {
      if (!music.paused) {
        music.pause();
      }
      music.currentTime = 0;
    }

    this.lastMusicState = state;
  }

  /** Rewinds and starts the background music from the beginning on explicit restart. */
  restartMusic(): void {
    this.syncGameState(GameState.PLAYING, true);
  }

  /** Plays the uploaded glass-breaking sound effect when lines are cleared. */
  playGlassBreaking(): void {
    if (this.muted) return;

    if (this.ctx && this.ctx.state === 'running' && this.glassBuffer) {
      const src = this.ctx.createBufferSource();
      const gain = this.ctx.createGain();
      src.buffer = this.glassBuffer;
      gain.gain.setValueAtTime(0.85, this.ctx.currentTime);
      src.connect(gain);
      gain.connect(this.ctx.destination);
      src.start();
      return;
    }

    const fallback = this.ensureGlassFallback();
    if (fallback) {
      fallback.muted = this.muted;
      fallback.currentTime = 0;
      fallback.play().catch(() => {});
    }
  }

  /**
   * Schedules a short enveloped oscillator tone with a fast attack and
   * exponential decay to prevent audible clicks.
   */
  private tone(
    freq: number,
    duration: number,
    type: OscillatorType,
    gainVal: number,
    delay = 0
  ): void {
    if (this.muted || !this.ctx || this.ctx.state !== 'running') return;

    const now = this.ctx.currentTime + delay;
    const osc = this.ctx.createOscillator();
    const gain = this.ctx.createGain();

    osc.type = type;
    osc.frequency.setValueAtTime(freq, now);

    gain.gain.setValueAtTime(0.0001, now);
    gain.gain.exponentialRampToValueAtTime(gainVal, now + 0.008);
    gain.gain.exponentialRampToValueAtTime(0.0001, now + duration);

    osc.connect(gain);
    gain.connect(this.ctx.destination);

    osc.start(now);
    osc.stop(now + duration + 0.01);
  }

  /** Triggers corresponding audio cues for any C++ event flags emitted this tick. */
  playEvents(mask: number): void {
    if (this.muted || mask === 0) return;

    if (mask & WasmEvent.MOVE) {
      this.tone(240, 0.035, 'triangle', 0.025);
    }
    if (mask & WasmEvent.ROTATE) {
      this.tone(360, 0.045, 'sine', 0.035);
    }
    if (mask & WasmEvent.LOCK) {
      this.tone(145, 0.06, 'triangle', 0.045);
    }
    if (mask & (WasmEvent.CLEAR | WasmEvent.QUAD)) {
      this.playGlassBreaking();
    }
    if (mask & WasmEvent.GAME_OVER) {
      this.tone(311.13, 0.18, 'triangle', 0.05, 0);
      this.tone(261.63, 0.22, 'triangle', 0.05, 0.14);
      this.tone(196.0, 0.34, 'triangle', 0.055, 0.28);
    }
  }
}

export const soundEngine = new SoundEngine();
