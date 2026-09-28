/*
 * Picture Processing Unit (Cinoop: gpu.c).
 *
 * Like Cinoop, the PPU is a state machine stepped with the number of cycles
 * the CPU just used, and it draws one complete scanline at a time:
 *
 *   mode 2 (OAM scan)    80 cycles  \
 *   mode 3 (transfer)   172 cycles   } lines 0-143, 456 cycles per line
 *   mode 0 (H-Blank)    204 cycles  /   the line is rendered entering mode 0
 *   mode 1 (V-Blank)    10 lines of 456 cycles (lines 144-153)
 *
 * Cinoop keeps a pre-decoded tile cache that is updated on every VRAM write.
 * Here tiles are decoded straight from VRAM while drawing - simpler and
 * fast enough on a PC.
 *
 * Additions beyond Cinoop needed for real games: the window, 8x16 sprites,
 * sprite priority and the 10-per-line limit, STAT interrupts, LYC, and the
 * LCD on/off behaviour.
 */

#include "ppu.h"

#include <string.h>

#include "interrupts.h"
#include "memory.h"

struct ppu ppu;
uint8_t ppuBackBuffer[SCREEN_HEIGHT][SCREEN_WIDTH];
uint8_t ppuFrontBuffer[SCREEN_HEIGHT][SCREEN_WIDTH];

void ppuReset(void) {
    memset(&ppu, 0, sizeof(ppu));
    /* State after the DMG boot ROM. */
    ppu.lcdc = 0x91;
    ppu.bgp = 0xFC;
    ppu.obp0 = 0xFF;
    ppu.obp1 = 0xFF;
    ppu.ly = 0;
    ppu.mode = PPU_MODE_OAM;
    ppu.modeClock = 0;
    memset(ppuBackBuffer, 0, sizeof(ppuBackBuffer));
    memset(ppuFrontBuffer, 0, sizeof(ppuFrontBuffer));
}

/* ------------------------------------------------------------------------- */
/* STAT / LYC                                                                  */
/* ------------------------------------------------------------------------- */

/*
 * The four STAT interrupt sources are ORed into one internal line; the
 * interrupt fires on its rising edge only ("STAT blocking").
 */
static void updateStatLine(void) {
    bool coincidence = (ppu.ly == ppu.lyc);
    bool line = false;

    if (!(ppu.lcdc & LCDC_LCD_ENABLE)) {
        ppu.statLine = false;
        return;
    }

    if ((ppu.stat & STAT_LYC_INT) && coincidence) line = true;
    if ((ppu.stat & STAT_HBLANK_INT) && ppu.mode == PPU_MODE_HBLANK) line = true;
    if ((ppu.stat & STAT_VBLANK_INT) && ppu.mode == PPU_MODE_VBLANK) line = true;
    if ((ppu.stat & STAT_OAM_INT) && ppu.mode == PPU_MODE_OAM) line = true;

    if (line && !ppu.statLine) requestInterrupt(INTERRUPT_LCDSTAT);
    ppu.statLine = line;
}

static void setMode(enum ppuMode mode) {
    ppu.mode = mode;
    updateStatLine();
}

/* ------------------------------------------------------------------------- */
/* Rendering                                                                  */
/* ------------------------------------------------------------------------- */

/* Colour number (0-3) of pixel (x, y) inside a tile. */
static uint8_t tilePixel(uint16_t tileAddress, int x, int y) {
    uint8_t low = vram[tileAddress + y * 2];
    uint8_t high = vram[tileAddress + y * 2 + 1];
    int bit = 7 - x;
    return (uint8_t)((((high >> bit) & 1) << 1) | ((low >> bit) & 1));
}

/* VRAM offset of a BG/window tile, honouring the LCDC.4 addressing mode. */
static uint16_t bgTileAddress(uint8_t tileNumber) {
    if (ppu.lcdc & LCDC_TILE_DATA) return (uint16_t)(tileNumber * 16);    /* 8000, unsigned */
    return (uint16_t)(0x1000 + (int8_t)tileNumber * 16);                  /* 8800, signed around 9000 */
}

static uint8_t applyPalette(uint8_t palette, uint8_t colour) {
    return (uint8_t)((palette >> (colour * 2)) & 0x03);
}

void ppuRenderScanline(void) {
    uint8_t bgColour[SCREEN_WIDTH]; /* raw colour numbers, needed for sprite priority */
    uint8_t *line = ppuBackBuffer[ppu.ly];
    int x;

    if (ppu.ly >= SCREEN_HEIGHT) return;

    memset(bgColour, 0, sizeof(bgColour));

    /* --- Background and window ---------------------------------------- */
    /* On the DMG, LCDC.0 = 0 blanks both background and window to colour 0. */
    if (ppu.lcdc & LCDC_BG_ENABLE) {
        uint16_t bgMap = (ppu.lcdc & LCDC_BG_MAP) ? 0x1C00 : 0x1800;
        uint16_t windowMap = (ppu.lcdc & LCDC_WINDOW_MAP) ? 0x1C00 : 0x1800;
        int y = (ppu.ly + ppu.scy) & 0xFF;
        int windowX = ppu.wx - 7;
        bool windowVisible = (ppu.lcdc & LCDC_WINDOW_ENABLE) && ppu.ly >= ppu.wy && ppu.wx <= 166;

        for (x = 0; x < SCREEN_WIDTH; x++) {
            uint16_t tile;
            int px, py;
            uint8_t colour;

            if (windowVisible && x >= windowX) {
                px = x - windowX;
                py = ppu.windowLine;
                tile = bgTileAddress(vram[windowMap + (py >> 3) * 32 + (px >> 3)]);
            } else {
                px = (x + ppu.scx) & 0xFF;
                py = y;
                /* Cinoop's fix: map row = ((line + scrollY) & 255) >> 3, times 32 */
                tile = bgTileAddress(vram[bgMap + (py >> 3) * 32 + (px >> 3)]);
            }

            colour = tilePixel(tile, px & 7, py & 7);
            bgColour[x] = colour;
            line[x] = applyPalette(ppu.bgp, colour);
        }

        /* The window has its own line counter: it only advances on lines
         * where the window was actually drawn. */
        if (windowVisible) ppu.windowLine++;
    } else {
        memset(line, 0, SCREEN_WIDTH); /* white, regardless of BGP */
    }

    /* --- Sprites ------------------------------------------------------- */
    if (ppu.lcdc & LCDC_OBJ_ENABLE) {
        int height = (ppu.lcdc & LCDC_OBJ_SIZE) ? 16 : 8;
        int visible[10];
        int count = 0;
        int i, j;

        /* OAM scan: the first 10 sprites (in OAM order) on this line. */
        for (i = 0; i < 40 && count < 10; i++) {
            int spriteY = oam[i * 4] - 16;
            if (ppu.ly >= spriteY && ppu.ly < spriteY + height) visible[count++] = i;
        }

        /* DMG priority: smaller X wins, ties go to the lower OAM index.
         * Insertion sort keeps equal X in OAM order (stable). */
        for (i = 1; i < count; i++) {
            int current = visible[i];
            j = i - 1;
            while (j >= 0 && oam[visible[j] * 4 + 1] > oam[current * 4 + 1]) {
                visible[j + 1] = visible[j];
                j--;
            }
            visible[j + 1] = current;
        }

        for (x = 0; x < SCREEN_WIDTH; x++) {
            for (i = 0; i < count; i++) {
                const uint8_t *sprite = &oam[visible[i] * 4];
                int spriteX = sprite[1] - 8;
                int spriteY = sprite[0] - 16;
                uint8_t tileNumber = sprite[2];
                uint8_t attributes = sprite[3];
                int row, column;
                uint8_t colour;

                if (x < spriteX || x >= spriteX + 8) continue;

                row = ppu.ly - spriteY;
                column = x - spriteX;
                if (attributes & 0x40) row = height - 1 - row;   /* Y flip */
                if (attributes & 0x20) column = 7 - column;      /* X flip */
                if (height == 16) tileNumber &= 0xFE;            /* bit 0 ignored */

                /* Sprites always use 8000 addressing. Row 8-15 falls into
                 * the next tile automatically. */
                colour = tilePixel((uint16_t)(tileNumber * 16), column, row);
                if (colour == 0) continue; /* transparent: try the next sprite */

                /* The highest-priority opaque sprite pixel decides; if its
                 * BG-over-OBJ flag is set, BG colours 1-3 hide it. */
                if (!((attributes & 0x80) && bgColour[x] != 0)) {
                    line[x] = applyPalette((attributes & 0x10) ? ppu.obp1 : ppu.obp0, colour);
                }
                break;
            }
        }
    }
}

/* ------------------------------------------------------------------------- */
/* Timing                                                                     */
/* ------------------------------------------------------------------------- */

static void setLY(uint8_t ly) {
    ppu.ly = ly;
    updateStatLine();
}

void ppuStep(int cycles) {
    if (!(ppu.lcdc & LCDC_LCD_ENABLE)) return;

    ppu.modeClock += cycles;

    for (;;) {
        switch (ppu.mode) {
            case PPU_MODE_OAM:
                if (ppu.modeClock < PPU_OAM_CYCLES) return;
                ppu.modeClock -= PPU_OAM_CYCLES;
                setMode(PPU_MODE_TRANSFER);
                break;

            case PPU_MODE_TRANSFER:
                if (ppu.modeClock < PPU_TRANSFER_CYCLES) return;
                ppu.modeClock -= PPU_TRANSFER_CYCLES;
                ppuRenderScanline();
                setMode(PPU_MODE_HBLANK);
                break;

            case PPU_MODE_HBLANK:
                if (ppu.modeClock < PPU_HBLANK_CYCLES) return;
                ppu.modeClock -= PPU_HBLANK_CYCLES;
                ppu.ly++;
                if (ppu.ly == SCREEN_HEIGHT) {
                    /* Entering V-Blank: publish the finished frame. */
                    memcpy(ppuFrontBuffer, ppuBackBuffer, sizeof(ppuFrontBuffer));
                    ppu.frameReady = true;
                    ppu.frames++;
                    requestInterrupt(INTERRUPT_VBLANK);
                    ppu.mode = PPU_MODE_VBLANK;
                } else {
                    ppu.mode = PPU_MODE_OAM;
                }
                updateStatLine();
                break;

            case PPU_MODE_VBLANK:
                if (ppu.modeClock < PPU_LINE_CYCLES) return;
                ppu.modeClock -= PPU_LINE_CYCLES;
                if (ppu.ly == PPU_LINES - 1) {
                    ppu.ly = 0;
                    ppu.windowLine = 0;
                    ppu.mode = PPU_MODE_OAM;
                    updateStatLine();
                } else {
                    setLY((uint8_t)(ppu.ly + 1));
                }
                break;
        }
    }
}

/* ------------------------------------------------------------------------- */
/* Registers FF40-FF4B                                                        */
/* ------------------------------------------------------------------------- */

uint8_t ppuRead(uint16_t address) {
    switch (address) {
        case 0xFF40: return ppu.lcdc;
        case 0xFF41: {
            uint8_t mode = (ppu.lcdc & LCDC_LCD_ENABLE) ? (uint8_t)ppu.mode : 0;
            uint8_t coincidence = (ppu.ly == ppu.lyc) ? STAT_COINCIDENCE : 0;
            return (uint8_t)(0x80 | (ppu.stat & 0x78) | coincidence | mode);
        }
        case 0xFF42: return ppu.scy;
        case 0xFF43: return ppu.scx;
        case 0xFF44: return ppu.ly;
        case 0xFF45: return ppu.lyc;
        case 0xFF46: return ppu.dma;
        case 0xFF47: return ppu.bgp;
        case 0xFF48: return ppu.obp0;
        case 0xFF49: return ppu.obp1;
        case 0xFF4A: return ppu.wy;
        case 0xFF4B: return ppu.wx;
        default:     return 0xFF;
    }
}

void ppuWrite(uint16_t address, uint8_t value) {
    switch (address) {
        case 0xFF40: {
            bool wasOn = (ppu.lcdc & LCDC_LCD_ENABLE) != 0;
            bool isOn = (value & LCDC_LCD_ENABLE) != 0;
            ppu.lcdc = value;
            if (wasOn && !isOn) {
                /* LCD off: LY resets to 0, the screen goes blank (white). */
                ppu.ly = 0;
                ppu.modeClock = 0;
                ppu.windowLine = 0;
                ppu.mode = PPU_MODE_HBLANK;
                ppu.statLine = false;
                memset(ppuFrontBuffer, 0, sizeof(ppuFrontBuffer));
                ppu.frameReady = true;
            } else if (!wasOn && isOn) {
                ppu.ly = 0;
                ppu.modeClock = 0;
                ppu.windowLine = 0;
                ppu.mode = PPU_MODE_OAM;
                updateStatLine();
            }
            break;
        }
        case 0xFF41:
            ppu.stat = value & 0x78; /* bits 0-2 are read-only */
            updateStatLine();
            break;
        case 0xFF42: ppu.scy = value; break;
        case 0xFF43: ppu.scx = value; break;
        case 0xFF44: break; /* LY is read-only */
        case 0xFF45:
            ppu.lyc = value;
            updateStatLine();
            break;
        case 0xFF46: ppu.dma = value; break; /* the copy is done by memory.c */
        case 0xFF47: ppu.bgp = value; break;
        case 0xFF48: ppu.obp0 = value; break;
        case 0xFF49: ppu.obp1 = value; break;
        case 0xFF4A: ppu.wy = value; break;
        case 0xFF4B: ppu.wx = value; break;
        default: break;
    }
}
