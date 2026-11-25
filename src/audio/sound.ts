/**
 * Lightweight Web Audio API sound synthesizer.
 *
 * Translates C++ WebAssembly `WasmEvent` bitmask flags into subtle, zero-asset
 * synthesized tactile tones (moves, rotations, tile locks, line clears, quads,
 * and game-over chimes) while respecting user mute preferences and browser
 * autoplay unlock policies.
 */
import { WasmEvent } from '../wasm/engine';

class SoundEngine {
  private ctx: AudioContext | null = null;
  private muted = false;

  /** Enables or mutes all synthesized game audio. */
  setMuted(muted: boolean): void {
    this.muted = muted;
  }

  isMuted(): boolean {
    return this.muted;
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
    if (mask & WasmEvent.CLEAR) {
      this.tone(523.25, 0.12, 'sine', 0.055, 0);
      this.tone(659.25, 0.14, 'sine', 0.055, 0.05);
    }
    if (mask & WasmEvent.QUAD) {
      this.tone(523.25, 0.12, 'sine', 0.06, 0);
      this.tone(659.25, 0.12, 'sine', 0.06, 0.05);
      this.tone(783.99, 0.14, 'sine', 0.06, 0.1);
      this.tone(1046.5, 0.22, 'sine', 0.07, 0.15);
    }
    if (mask & WasmEvent.GAME_OVER) {
      this.tone(311.13, 0.18, 'triangle', 0.05, 0);
      this.tone(261.63, 0.22, 'triangle', 0.05, 0.14);
      this.tone(196.0, 0.34, 'triangle', 0.055, 0.28);
    }
  }
}

export const soundEngine = new SoundEngine();
