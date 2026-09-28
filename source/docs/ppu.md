# PPU (graphics)

Files: `include/ppu.h`, `src/ppu.c`, `src/display.c`

## Timing

Each of the 154 lines lasts 456 cycles, so a frame is 70224 cycles
(about 59.73 Hz). Following Cinoop's GPU:

```
line 0..143:   mode 2 (OAM scan) 80  ->  mode 3 (drawing) 172  ->  mode 0 (H-Blank) 204
line 144..153: mode 1 (V-Blank), 456 each
```

`ppuStep(cycles)` adds the cycles to `modeClock` and advances through as many
mode changes as the cycles cover. At these transitions:

- **leaving mode 3**, the line is drawn with `ppuRenderScanline()`;
- **LY becomes 144**, the frame is copied to `ppuFrontBuffer`, `frameReady`
  is set and the V-Blank interrupt is requested;
- **after line 153**, LY returns to 0 and the window line counter resets.

## LCD control (LCDC, FF40)

| Bit | Meaning |
|-----|---------|
| 7 | LCD on. Turning it off sets LY=0 and mode 0 and blanks the screen; the PPU stops |
| 6 | Window tile map: 0 = 9800, 1 = 9C00 |
| 5 | Window enable |
| 4 | BG/window tile data: 1 = 8000 unsigned, 0 = 8800 signed around 9000 |
| 3 | BG tile map: 0 = 9800, 1 = 9C00 |
| 2 | Sprite size: 0 = 8×8, 1 = 8×16 |
| 1 | Sprites on |
| 0 | BG and window on. On the DMG, 0 makes both white |

## STAT (FF41), LY (FF44) and LYC (FF45)

STAT bits 0–1 show the mode and bit 2 shows LY==LYC. Bits 3–6 enable the
interrupt sources: mode 0, mode 1, mode 2 and LYC. All enabled sources are
ORed into a single line, and the STAT interrupt fires only on its rising
edge. This is "STAT blocking". LY is read-only.

## Rendering a scanline

1. **Background.** For each x, compute `px = (x + SCX) & 255` and
   `py = (LY + SCY) & 255`. The tile is `map[(py/8)*32 + px/8]`. Its colour
   number comes from the two bitplanes at `tile*16 + (py%8)*2`, and the shade
   from `BGP >> (colour*2) & 3`.
2. **Window.** It is drawn when enabled, `LY >= WY` and `WX <= 166`, starting
   at screen x = WX−7. It uses its own line counter, which only advances on
   lines where the window was drawn.
3. **Sprites.** The OAM scan picks the first 10 sprites (in OAM order) that
   cover LY. Priority on the DMG goes to the smaller X, with ties going to
   the lower OAM index. For each pixel, the highest-priority sprite with a
   non-zero colour decides:
   - with X/Y flip, 8×16 mode (tile number bit 0 ignored) and OBP0 or OBP1;
   - if its BG-over-OBJ attribute (bit 7) is set and the BG colour number is
     1–3, the BG pixel is shown instead.

The raw BG colour numbers are kept for the priority check, because priority
depends on the colour number, not on the shade after the palette.

## Pipeline to the screen

```
VRAM tiles + maps + OAM
   -> ppuRenderScanline()        colour numbers -> palettes -> shade 0..3
   -> ppuBackBuffer[144][160]
   -> (at V-Blank) ppuFrontBuffer
   -> display.c: shade -> ARGB (DMG green palette)
   -> SDL streaming texture, 160×144
   -> SDL_RenderCopy scaled to the window (nearest neighbour, 10:9 kept)
```

The emulator never scales. Only the SDL renderer does, when presenting.

## Simplifications

- Mode 3 always lasts 172 cycles. On hardware it gets longer with SCX, the
  window and sprites.
- Each line is drawn in one go, so register changes in the middle of a line
  take effect on the next line.
- VRAM and OAM are not locked during modes 2 and 3.
