#ifndef GB_PPU_H
#define GB_PPU_H

#include <stdbool.h>
#include <stdint.h>

#define SCREEN_WIDTH  160
#define SCREEN_HEIGHT 144

/* Timing in T-cycles (from Cinoop's GPU and Pan Docs). */
#define PPU_OAM_CYCLES      80   /* mode 2 */
#define PPU_TRANSFER_CYCLES 172  /* mode 3 */
#define PPU_HBLANK_CYCLES   204  /* mode 0 */
#define PPU_LINE_CYCLES     456
#define PPU_LINES           154
#define PPU_FRAME_CYCLES    (PPU_LINE_CYCLES * PPU_LINES) /* 70224 */

enum ppuMode {
    PPU_MODE_HBLANK = 0,
    PPU_MODE_VBLANK = 1,
    PPU_MODE_OAM = 2,
    PPU_MODE_TRANSFER = 3,
};

/* LCDC bits */
#define LCDC_BG_ENABLE      0x01
#define LCDC_OBJ_ENABLE     0x02
#define LCDC_OBJ_SIZE       0x04
#define LCDC_BG_MAP         0x08
#define LCDC_TILE_DATA      0x10
#define LCDC_WINDOW_ENABLE  0x20
#define LCDC_WINDOW_MAP     0x40
#define LCDC_LCD_ENABLE     0x80

/* STAT bits */
#define STAT_MODE_MASK      0x03
#define STAT_COINCIDENCE    0x04
#define STAT_HBLANK_INT     0x08
#define STAT_VBLANK_INT     0x10
#define STAT_OAM_INT        0x20
#define STAT_LYC_INT        0x40

struct ppu {
    uint8_t lcdc, stat, scy, scx, ly, lyc, dma, bgp, obp0, obp1, wy, wx;
    enum ppuMode mode;
    int modeClock;        /* cycles spent in the current mode           */
    int windowLine;       /* internal window line counter               */
    bool statLine;        /* STAT interrupt line (for edge detection)   */
    bool frameReady;      /* a complete frame is in ppuFrontBuffer      */
    uint64_t frames;
};

extern struct ppu ppu;

/* Shade indices 0 (lightest) .. 3 (darkest), after palette mapping. */
extern uint8_t ppuBackBuffer[SCREEN_HEIGHT][SCREEN_WIDTH];  /* being drawn  */
extern uint8_t ppuFrontBuffer[SCREEN_HEIGHT][SCREEN_WIDTH]; /* last frame   */

void ppuReset(void);
void ppuStep(int cycles);
uint8_t ppuRead(uint16_t address);
void ppuWrite(uint16_t address, uint8_t value);

/* Render a single scanline into ppuBackBuffer (exposed for tests). */
void ppuRenderScanline(void);

#endif
