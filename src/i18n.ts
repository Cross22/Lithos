/**
 * Internationalization dictionary and browser language detection for Lithos 10x20.
 * Automatically selects German ('de') when a German browser locale is detected,
 * otherwise defaults to English ('en').
 */

export type Language = 'en' | 'de';

export interface Translations {
  locale: string;
  engineErrorTitle: string;
  engineErrorFallback: string;
  boardAriaLabel: string;
  loadingEngine: string;
  titleTagline: string;
  startGame: string;
  startHint: string;
  pausedTitle: string;
  pausedSubtitle: string;
  resume: string;
  restart: string;
  restartAriaLabel: string;
  restartTitle: string;
  gameOverTitle: string;
  formatLinesAndLevel: (lines: number, level: number) => string;
  score: string;
  timeShort: string;
  elapsedTime: string;
  best: string;
  linesLabel: string;
  levelShort: string;
  playAgain: string;
  next: string;
  muteAudio: string;
  unmuteAudio: string;
  hudAriaLabel: string;
  pause: string;
  controls: {
    move: string;
    rotate: string;
    drop: string;
    hardDrop: string;
    hardDropKey: string;
    pauseReset: string;
  };
}

const TRANSLATIONS: Record<Language, Translations> = {
  en: {
    locale: 'en-US',
    engineErrorTitle: 'Unable to Initialize Game Engine',
    engineErrorFallback: 'Unable to load WebAssembly game engine.',
    boardAriaLabel: 'Grey marble border framing the 10 by 20 playing field',
    loadingEngine: 'Loading WebAssembly engine...',
    titleTagline: 'Stack falling stone tiles across the 10x20 pit and clear rows.',
    startGame: 'Start Game',
    startHint: 'Enter or Space to start',
    pausedTitle: 'Paused',
    pausedSubtitle: 'Timer and gravity paused',
    resume: 'Resume',
    restart: 'Restart',
    restartAriaLabel: 'Restart game',
    restartTitle: 'Restart (R)',
    gameOverTitle: 'Game Over',
    formatLinesAndLevel: (lines, level) =>
      `${lines} ${lines === 1 ? 'line' : 'lines'} / Lv ${level}`,
    score: 'Score',
    timeShort: 'Time',
    elapsedTime: 'Elapsed Time',
    best: 'Best',
    linesLabel: 'Lines',
    levelShort: 'Lv',
    playAgain: 'Play Again',
    next: 'Next',
    muteAudio: 'Mute audio',
    unmuteAudio: 'Unmute audio',
    hudAriaLabel: 'Next shape, score, elapsed time, and game controls',
    pause: 'Pause',
    controls: {
      move: 'Move',
      rotate: 'Rotate',
      drop: 'Drop',
      hardDrop: 'Hard',
      hardDropKey: 'Space',
      pauseReset: 'Pause/Reset',
    },
  },
  de: {
    locale: 'de-DE',
    engineErrorTitle: 'Spiel-Engine nicht verfügbar',
    engineErrorFallback: 'WebAssembly-Spiel-Engine konnte nicht geladen werden.',
    boardAriaLabel: 'Grauer Marmorrahmen um das 10-mal-20-Spielfeld',
    loadingEngine: 'Lade WebAssembly-Engine...',
    titleTagline: 'Staple fallende Steinblöcke im 10x20-Feld und räume Reihen ab.',
    startGame: 'Spiel starten',
    startHint: 'Enter oder Leertaste zum Starten',
    pausedTitle: 'Pausiert',
    pausedSubtitle: 'Zeit und Schwerkraft pausiert',
    resume: 'Fortsetzen',
    restart: 'Neustart',
    restartAriaLabel: 'Spiel neu starten',
    restartTitle: 'Neustart (R)',
    gameOverTitle: 'Spiel vorbei',
    formatLinesAndLevel: (lines, level) =>
      `${lines} ${lines === 1 ? 'Reihe' : 'Reihen'} / Lv ${level}`,
    score: 'Punkte',
    timeShort: 'Zeit',
    elapsedTime: 'Verstrichene Zeit',
    best: 'Rekord',
    linesLabel: 'Reihen',
    levelShort: 'Lv',
    playAgain: 'Nochmal spielen',
    next: 'Nächster',
    muteAudio: 'Ton stummschalten',
    unmuteAudio: 'Ton aktivieren',
    hudAriaLabel: 'Nächster Stein, Punkte, verstrichene Zeit und Steuerung',
    pause: 'Pause',
    controls: {
      move: 'Bewegen',
      rotate: 'Drehen',
      drop: 'Senken',
      hardDrop: 'Abwurf',
      hardDropKey: 'Leertaste',
      pauseReset: 'Pause/Neu',
    },
  },
};

/** Detects whether the active browser language is German (`de`, `de-DE`, `de-AT`, `de-CH`, etc.). */
export function detectBrowserLanguage(): Language {
  if (typeof navigator === 'undefined') return 'en';
  const candidates =
    Array.isArray(navigator.languages) && navigator.languages.length > 0
      ? navigator.languages
      : [navigator.language];
  const primary = (candidates[0] ?? '').trim().toLowerCase();
  if (primary === 'de' || primary.startsWith('de-')) {
    return 'de';
  }
  return 'en';
}

export function getTranslations(lang: Language): Translations {
  return TRANSLATIONS[lang];
}
