/**
 * 2D Canvas renderers for the 10x20 tile grid and 4x4 next-tile preview.
 *
 * Reads cell byte codes directly from the C++ WebAssembly `renderGrid` and
 * `nextGrid` linear-memory views:
 *   - 0        : Empty cell
 *   - 1..7     : Solid locked or active falling tetromino tile
 *   - 17..23   : Ghost landing projection (16 + pieceType)
 *   - 33..39   : Clearing row highlight flash (32 + pieceType)
 */
import { useEffect, useRef } from 'react';

interface TilePalette {
  base: string;
  light: string;
  dark: string;
  ghost: string;
}

/**
 * Mineral-inspired palette for the 7 tetromino types (indices 1..7).
 * Each tile uses a main face plus lighter top/left and darker bottom/right
 * bevel shades, alongside an RGB triplet for translucent ghost outlines.
 */
const TILE_PALETTE: Record<number, TilePalette> = {
  1: { base: '#2CB1BC', light: '#65D6E0', dark: '#18757E', ghost: '44, 177, 188' }, // I - Mineral Teal
  2: { base: '#D99B26', light: '#F3C15A', dark: '#966611', ghost: '217, 155, 38' },  // O - Ochre Amber
  3: { base: '#8B68C8', light: '#B49AE4', dark: '#593D8E', ghost: '139, 104, 200' }, // T - Amethyst
  4: { base: '#34A876', light: '#64CFA0', dark: '#1F6E4C', ghost: '52, 168, 118' },  // S - Jade
  5: { base: '#D05353', light: '#EB8585', dark: '#8C2F2F', ghost: '208, 83, 83' },   // Z - Jasper Red
  6: { base: '#467FD0', light: '#79A8EB', dark: '#29518E', ghost: '70, 127, 208' },  // J - Cobalt
  7: { base: '#D4763B', light: '#EEA06E', dark: '#8F481C', ghost: '212, 118, 59' },  // L - Terracotta
};

/**
 * Draws a single beveled square tile at pixel coordinates `(x, y)`.
 * When `isClearing` is true, renders a solid alabaster flash frame during the
 * C++ 130ms line-clear animation window.
 */
export function drawSquareTile(
  ctx: CanvasRenderingContext2D,
  x: number,
  y: number,
  size: number,
  palette: TilePalette,
  isClearing = false
) {
  // Subtle 1px+ mortar gap between adjacent tiles
  const pad = Math.max(1, Math.floor(size * 0.04));
  const innerX = x + pad;
  const innerY = y + pad;
  const innerSize = size - pad * 2;
  const bevel = Math.max(2, Math.floor(innerSize * 0.14));

  if (isClearing) {
    ctx.fillStyle = '#F8FAFC';
    ctx.fillRect(innerX, innerY, innerSize, innerSize);
    return;
  }

  // Center tile face
  ctx.fillStyle = palette.base;
  ctx.fillRect(innerX, innerY, innerSize, innerSize);

  // Top chamfer highlight
  ctx.fillStyle = palette.light;
  ctx.beginPath();
  ctx.moveTo(innerX, innerY);
  ctx.lineTo(innerX + innerSize, innerY);
  ctx.lineTo(innerX + innerSize - bevel, innerY + bevel);
  ctx.lineTo(innerX + bevel, innerY + bevel);
  ctx.closePath();
  ctx.fill();

  // Left chamfer highlight
  ctx.beginPath();
  ctx.moveTo(innerX, innerY);
  ctx.lineTo(innerX + bevel, innerY + bevel);
  ctx.lineTo(innerX + bevel, innerY + innerSize - bevel);
  ctx.lineTo(innerX, innerY + innerSize);
  ctx.closePath();
  ctx.fill();

  // Bottom chamfer shadow
  ctx.fillStyle = palette.dark;
  ctx.beginPath();
  ctx.moveTo(innerX, innerY + innerSize);
  ctx.lineTo(innerX + bevel, innerY + innerSize - bevel);
  ctx.lineTo(innerX + innerSize - bevel, innerY + innerSize - bevel);
  ctx.lineTo(innerX + innerSize, innerY + innerSize);
  ctx.closePath();
  ctx.fill();

  // Right chamfer shadow
  ctx.beginPath();
  ctx.moveTo(innerX + innerSize, innerY);
  ctx.lineTo(innerX + innerSize, innerY + innerSize);
  ctx.lineTo(innerX + innerSize - bevel, innerY + innerSize - bevel);
  ctx.lineTo(innerX + innerSize - bevel, innerY + bevel);
  ctx.closePath();
  ctx.fill();

  // Crisp 0.5px-aligned inner facet hairline
  ctx.strokeStyle = 'rgba(255, 255, 255, 0.14)';
  ctx.lineWidth = 1;
  ctx.strokeRect(
    innerX + bevel + 0.5,
    innerY + bevel + 0.5,
    Math.max(1, innerSize - bevel * 2 - 1),
    Math.max(1, innerSize - bevel * 2 - 1)
  );
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
 * Renders the 10x20 playing field on a HiDPI-aware HTML5 Canvas.
 */
export function GameCanvas({ tileSize, cols, rows, renderGrid, frameVersion }: GameCanvasProps) {
  const canvasRef = useRef<HTMLCanvasElement | null>(null);
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

    // 3. Draw ghost landing guides, clearing flashes, and solid stone tiles
    if (renderGrid && renderGrid.length >= cols * rows) {
      for (let r = 0; r < rows; r++) {
        for (let c = 0; c < cols; c++) {
          const val = renderGrid[r * cols + c];
          if (val === 0) continue;

          const px = c * tileSize;
          const py = r * tileSize;

          if (val >= 17 && val <= 23) {
            // Ghost piece projection (C++ encodes as 17..23)
            const typeId = val - 16;
            const pal = TILE_PALETTE[typeId];
            if (!pal) continue;
            const inset = Math.max(2, Math.floor(tileSize * 0.08));
            ctx.fillStyle = `rgba(${pal.ghost}, 0.1)`;
            ctx.fillRect(px + inset, py + inset, tileSize - inset * 2, tileSize - inset * 2);
            ctx.strokeStyle = `rgba(${pal.ghost}, 0.55)`;
            ctx.lineWidth = 1.25;
            ctx.strokeRect(
              px + inset + 0.5,
              py + inset + 0.5,
              tileSize - inset * 2 - 1,
              tileSize - inset * 2 - 1
            );
          } else if (val >= 33) {
            // Row currently in C++ line-clear flash state (33..39)
            const typeId = val - 32;
            const pal = TILE_PALETTE[typeId] ?? TILE_PALETTE[1];
            drawSquareTile(ctx, px, py, tileSize, pal, true);
          } else if (val >= 1 && val <= 7) {
            // Standard solid tetromino cell (1..7)
            const pal = TILE_PALETTE[val];
            if (pal) {
              drawSquareTile(ctx, px, py, tileSize, pal, false);
            }
          }
        }
      }
    }

    ctx.restore();
  }, [tileSize, cols, rows, widthCss, heightCss, renderGrid, frameVersion]);

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
}

/**
 * Renders the upcoming tetromino centered inside an 88x88px preview box.
 * Computes the tight bounding box of active cells so asymmetrical shapes
 * (such as the 4x1 I-piece or 2x2 O-piece) appear optically centered.
 */
export function NextPieceCanvas({ nextGrid, frameVersion }: NextPieceCanvasProps) {
  const canvasRef = useRef<HTMLCanvasElement | null>(null);

  useEffect(() => {
    const canvas = canvasRef.current;
    if (!canvas) return;

    const size = 88;
    const dpr = Math.min(window.devicePixelRatio || 1, 2);
    if (canvas.width !== size * dpr || canvas.height !== size * dpr) {
      canvas.width = size * dpr;
      canvas.height = size * dpr;
    }

    const ctx = canvas.getContext('2d');
    if (!ctx) return;

    ctx.save();
    ctx.scale(dpr, dpr);
    ctx.clearRect(0, 0, size, size);

    if (nextGrid && nextGrid.length >= 16) {
      let minR = 4;
      let maxR = -1;
      let minC = 4;
      let maxC = -1;
      let pieceType = 0;

      // Locate occupied bounding box within the 4x4 matrix
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

      if (pieceType > 0 && maxR >= minR && maxC >= minC) {
        const pal = TILE_PALETTE[pieceType];
        const cellSize = 18;
        const spanW = (maxC - minC + 1) * cellSize;
        const spanH = (maxR - minR + 1) * cellSize;
        const offsetX = Math.floor((size - spanW) / 2);
        const offsetY = Math.floor((size - spanH) / 2);

        for (let r = minR; r <= maxR; r++) {
          for (let c = minC; c <= maxC; c++) {
            if (nextGrid[r * 4 + c] > 0 && pal) {
              drawSquareTile(
                ctx,
                offsetX + (c - minC) * cellSize,
                offsetY + (r - minR) * cellSize,
                cellSize,
                pal,
                false
              );
            }
          }
        }
      }
    }

    ctx.restore();
  }, [nextGrid, frameVersion]);

  return (
    <canvas
      ref={canvasRef}
      style={{ width: '88px', height: '88px' }}
      className="block"
      aria-label="Next tile piece preview"
    />
  );
}
