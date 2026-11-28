/**
 * Spritesheet-backed 2D Canvas renderers for the 10x20 tile grid and 4x4 next-tile preview.
 *
 * Blits 64x64 square tile bitmaps from `/tiles-spritesheet.png` (512x64 horizontal strip):
 *   - Sprite 0   : Alabaster flash bitmap (clearing rows)
 *   - Sprites 1..7 : Mineral stone bitmaps for the 7 tetromino colors
 *
 * Reads cell byte codes directly from C++ WebAssembly linear memory:
 *   - 0        : Empty cell
 *   - 1..7     : Solid locked or active falling tile (drawn at alpha 1.0)
 *   - 17..23   : Ghost landing projection (drawn via alpha blending at alpha 0.28)
 *   - 33..39   : Clearing row highlight flash (draws sprite 0 at alpha 1.0)
 */
import { useEffect, useRef, useState } from 'react';

/** Pixel dimensions of each individual tile bitmap inside `/tiles-spritesheet.png`. */
const SPRITE_CELL_PX = 64;

/** Alpha opacity used when alpha-blending ghost tile bitmaps over the pit grid. */
const GHOST_TILE_ALPHA = 0.28;

const FALLBACK_COLORS = [
  '#F8FAFC',
  '#2CB1BC',
  '#D99B26',
  '#8B68C8',
  '#34A876',
  '#D05353',
  '#467FD0',
  '#D4763B',
];

/**
 * Shared loader hook for the tile bitmap spritesheet.
 * Caches a single `HTMLImageElement` instance across both canvases and triggers
 * a React re-render once the PNG bitmap finishes decoding.
 */
function useTileSpritesheet(): { sheet: HTMLImageElement | null; failed: boolean } {
  const [sheet, setSheet] = useState<HTMLImageElement | null>(null);
  const [failed, setFailed] = useState(false);

  useEffect(() => {
    let active = true;
    const img = new Image();
    img.src = '/tiles-spritesheet.png';
    img.onload = () => {
      if (active) {
        setSheet(img);
        setFailed(false);
      }
    };
    img.onerror = () => {
      if (active) {
        setFailed(true);
      }
    };
    return () => {
      active = false;
    };
  }, []);

  return { sheet, failed };
}

/**
 * Draws a single tile bitmap from the spritesheet onto `ctx` at `(dx, dy)`
 * scaled to `destSize x destSize`, with optional alpha blending.
 * Falls back to a solid color fill if the spritesheet PNG failed to load.
 */
function blitTileSprite(
  ctx: CanvasRenderingContext2D,
  sheet: HTMLImageElement | null,
  spriteIndex: number,
  dx: number,
  dy: number,
  destSize: number,
  alpha = 1
): void {
  ctx.globalAlpha = alpha;
  if (sheet) {
    const sx = spriteIndex * SPRITE_CELL_PX;
    ctx.drawImage(
      sheet,
      sx,
      0,
      SPRITE_CELL_PX,
      SPRITE_CELL_PX,
      dx,
      dy,
      destSize,
      destSize
    );
  } else {
    const pad = Math.max(1, Math.floor(destSize * 0.04));
    ctx.fillStyle = FALLBACK_COLORS[spriteIndex] ?? '#94A3B8';
    ctx.fillRect(dx + pad, dy + pad, destSize - pad * 2, destSize - pad * 2);
  }
}

interface GameCanvasProps {
  /** Computed square tile side length in CSS pixels. */
  tileSize: number;
  cols: number;
  rows: number;
  /** Zero-copy Uint8Array view of C++ `g.render_grid` (size: cols * rows). */
  renderGrid: Uint8Array | null;
  /** Monotonic frame counter triggering canvas redraws on every WASM tick. */
  frameVersion: number;
}

/**
 * Renders the 10x20 playing field on a HiDPI-aware HTML5 Canvas using the
 * bitmap tile spritesheet.
 */
export function GameCanvas({ tileSize, cols, rows, renderGrid, frameVersion }: GameCanvasProps) {
  const canvasRef = useRef<HTMLCanvasElement | null>(null);
  const { sheet: spritesheet, failed: sheetFailed } = useTileSpritesheet();
  const widthCss = tileSize * cols;
  const heightCss = tileSize * rows;

  useEffect(() => {
    const canvas = canvasRef.current;
    if (!canvas) return;

    // Scale backing store by devicePixelRatio (capped at 2x for performance)
    const dpr = Math.min(window.devicePixelRatio || 1, 2);
    const targetW = Math.round(widthCss * dpr);
    const targetH = Math.round(heightCss * dpr);

    if (canvas.width !== targetW || canvas.height !== targetH) {
      canvas.width = targetW;
      canvas.height = targetH;
    }

    const ctx = canvas.getContext('2d');
    if (!ctx) return;

    ctx.save();
    ctx.scale(dpr, dpr);
    ctx.imageSmoothingEnabled = true;
    ctx.imageSmoothingQuality = 'high';

    // 1. Fill recessed dark slate pit
    ctx.fillStyle = '#0B0D10';
    ctx.fillRect(0, 0, widthCss, heightCss);

    // 2. Draw crisp 1px grid lines for all 10 columns and 20 rows
    ctx.strokeStyle = 'rgba(255, 255, 255, 0.045)';
    ctx.lineWidth = 1;
    ctx.beginPath();
    for (let c = 1; c < cols; c++) {
      const gx = c * tileSize + 0.5;
      ctx.moveTo(gx, 0);
      ctx.lineTo(gx, heightCss);
    }
    for (let r = 1; r < rows; r++) {
      const gy = r * tileSize + 0.5;
      ctx.moveTo(0, gy);
      ctx.lineTo(widthCss, gy);
    }
    ctx.stroke();

    // 3. Blit tile bitmaps from the spritesheet (solid, alpha-blended ghost, or flash)
    if ((spritesheet || sheetFailed) && renderGrid && renderGrid.length >= cols * rows) {
      for (let r = 0; r < rows; r++) {
        for (let c = 0; c < cols; c++) {
          const val = renderGrid[r * cols + c];
          if (val === 0) continue;

          const px = c * tileSize;
          const py = r * tileSize;

          if (val >= 17 && val <= 23) {
            // Ghost landing projection: alpha-blend the matching color bitmap
            const spriteIdx = val - 16;
            blitTileSprite(ctx, spritesheet, spriteIdx, px, py, tileSize, GHOST_TILE_ALPHA);
          } else if (val >= 33) {
            // Row currently in C++ line-clear flash state: blit alabaster sprite (index 0)
            blitTileSprite(ctx, spritesheet, 0, px, py, tileSize, 1);
          } else if (val >= 1 && val <= 7) {
            // Standard solid tetromino cell (sprites 1..7)
            blitTileSprite(ctx, spritesheet, val, px, py, tileSize, 1);
          }
        }
      }
      ctx.globalAlpha = 1;
    }

    ctx.restore();
  }, [tileSize, cols, rows, widthCss, heightCss, renderGrid, frameVersion, spritesheet, sheetFailed]);

  return (
    <canvas
      ref={canvasRef}
      style={{ width: `${widthCss}px`, height: `${heightCss}px` }}
      className="block"
      aria-label="10 by 20 tile playing field"
    />
  );
}

interface NextPieceCanvasProps {
  /** Zero-copy Uint8Array view of C++ `g.next_grid` (4x4 = 16 bytes). */
  nextGrid: Uint8Array | null;
  frameVersion: number;
  /** Square CSS pixel dimensions of the preview canvas. */
  size?: number;
}

/**
 * Renders the upcoming tetromino centered inside a dynamically sized preview box
 * using the shared tile bitmap spritesheet.
 */
export function NextPieceCanvas({ nextGrid, frameVersion, size = 160 }: NextPieceCanvasProps) {
  const canvasRef = useRef<HTMLCanvasElement | null>(null);
  const { sheet: spritesheet, failed: sheetFailed } = useTileSpritesheet();

  useEffect(() => {
    const canvas = canvasRef.current;
    if (!canvas) return;

    const dpr = Math.min(window.devicePixelRatio || 1, 2);
    if (canvas.width !== size * dpr || canvas.height !== size * dpr) {
      canvas.width = size * dpr;
      canvas.height = size * dpr;
    }

    const ctx = canvas.getContext('2d');
    if (!ctx) return;

    ctx.save();
    ctx.scale(dpr, dpr);
    ctx.imageSmoothingEnabled = true;
    ctx.imageSmoothingQuality = 'high';
    ctx.clearRect(0, 0, size, size);

    if ((spritesheet || sheetFailed) && nextGrid && nextGrid.length >= 16) {
      let minR = 4;
      let maxR = -1;
      let minC = 4;
      let maxC = -1;
      let pieceType = 0;

      // Locate tight bounding box within the 4x4 matrix to optically center the piece
      for (let r = 0; r < 4; r++) {
        for (let c = 0; c < 4; c++) {
          const val = nextGrid[r * 4 + c];
          if (val > 0) {
            pieceType = val;
            if (r < minR) minR = r;
            if (r > maxR) maxR = r;
            if (c < minC) minC = c;
            if (c > maxC) maxC = c;
          }
        }
      }

      if (pieceType >= 1 && pieceType <= 7 && maxR >= minR && maxC >= minC) {
        const cellSize = Math.max(16, Math.floor((size - 16) / 4));
        const spanW = (maxC - minC + 1) * cellSize;
        const spanH = (maxR - minR + 1) * cellSize;
        const offsetX = Math.floor((size - spanW) / 2);
        const offsetY = Math.floor((size - spanH) / 2);

        for (let r = minR; r <= maxR; r++) {
          for (let c = minC; c <= maxC; c++) {
            if (nextGrid[r * 4 + c] > 0) {
              blitTileSprite(
                ctx,
                spritesheet,
                pieceType,
                offsetX + (c - minC) * cellSize,
                offsetY + (r - minR) * cellSize,
                cellSize,
                1
              );
            }
          }
        }
        ctx.globalAlpha = 1;
      }
    }

    ctx.restore();
  }, [nextGrid, frameVersion, size, spritesheet, sheetFailed]);

  return (
    <canvas
      ref={canvasRef}
      style={{ width: `${size}px`, height: `${size}px` }}
      className="block"
      aria-label="Next tile piece preview"
    />
  );
}
