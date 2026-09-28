/* PPU tests: modes, timing, LY/LYC, STAT interrupts, tile/window/sprite rendering. */

#include <string.h>

#include "interrupts.h"
#include "memory.h"
#include "ppu.h"
#include "test.h"

static void setup(void) {
    testResetMachine();
    interrupt.flags = 0;
    memset(vram, 0, VRAM_SIZE);
    memset(oam, 0, OAM_SIZE);
    ppu.bgp = 0xE4;   /* identity palette: colour n -> shade n */
    ppu.obp0 = 0xE4;
    ppu.obp1 = 0xE4;
}

/* Write one row of a tile: every pixel gets colour `colour` where mask bit is 1. */
static void tileRow(uint16_t tileOffset, int row, uint8_t mask, int colour) {
    vram[tileOffset + row * 2] = (colour & 1) ? mask : 0;
    vram[tileOffset + row * 2 + 1] = (colour & 2) ? mask : 0;
}

static void renderLine(int ly) {
    ppu.ly = (uint8_t)ly;
    ppuRenderScanline();
}

/* ---- Timing ----------------------------------------------------------- */

static void modeSequence(void) {
    setup();
    CHECK_EQ(ppu.mode, PPU_MODE_OAM);
    ppuStep(76);
    CHECK_EQ(readByte(0xFF41) & 3, 2);
    ppuStep(4);
    CHECK_EQ(ppu.mode, PPU_MODE_TRANSFER);
    CHECK_EQ(readByte(0xFF41) & 3, 3);
    ppuStep(172);
    CHECK_EQ(ppu.mode, PPU_MODE_HBLANK);
    CHECK_EQ(ppu.ly, 0);
    ppuStep(204);
    CHECK_EQ(ppu.mode, PPU_MODE_OAM);
    CHECK_EQ(ppu.ly, 1);
    CHECK_EQ(readByte(0xFF44), 1);
}

static void frameTiming(void) {
    int cycles = 0;
    setup();
    while (ppu.ly < 144) { ppuStep(4); cycles += 4; }
    CHECK_EQ(cycles, 144 * 456);
    CHECK_EQ(ppu.mode, PPU_MODE_VBLANK);
    CHECK(interrupt.flags & INTERRUPT_VBLANK);
    CHECK(ppu.frameReady);
    CHECK_EQ(ppu.frames, 1);

    while (ppu.ly != 0) { ppuStep(4); cycles += 4; }
    CHECK_EQ(cycles, PPU_FRAME_CYCLES);         /* 70224 cycles per frame */
    CHECK_EQ(ppu.mode, PPU_MODE_OAM);
}

static void largeStepsStayInSync(void) {
    setup();
    ppuStep(456 * 3 + 100);                     /* one big step crosses lines */
    CHECK_EQ(ppu.ly, 3);
    CHECK_EQ(ppu.mode, PPU_MODE_TRANSFER);
}

static void lycCoincidence(void) {
    setup();
    writeByte(0xFF45, 2);
    ppuStep(456);
    CHECK_EQ(readByte(0xFF41) & STAT_COINCIDENCE, 0);
    CHECK_EQ(interrupt.flags & INTERRUPT_LCDSTAT, 0);
    ppuStep(456);
    CHECK_EQ(ppu.ly, 2);
    CHECK(readByte(0xFF41) & STAT_COINCIDENCE);
    CHECK_EQ(interrupt.flags & INTERRUPT_LCDSTAT, 0); /* source not enabled */

    setup();
    writeByte(0xFF41, STAT_LYC_INT);
    writeByte(0xFF45, 2);
    ppuStep(912);
    CHECK(interrupt.flags & INTERRUPT_LCDSTAT);
}

static void statModeInterrupts(void) {
    setup();
    writeByte(0xFF41, STAT_HBLANK_INT);
    ppuStep(80 + 168);
    CHECK_EQ(interrupt.flags & INTERRUPT_LCDSTAT, 0);
    ppuStep(4);
    CHECK(interrupt.flags & INTERRUPT_LCDSTAT);

    setup();
    writeByte(0xFF41, STAT_VBLANK_INT);
    ppuStep(144 * 456);
    CHECK(interrupt.flags & INTERRUPT_LCDSTAT);
    CHECK(interrupt.flags & INTERRUPT_VBLANK);
}

static void statWritableBits(void) {
    setup();
    writeByte(0xFF41, 0xFF);
    CHECK_EQ(readByte(0xFF41) & 0xF8, 0xF8);   /* bit 7 always 1, 3-6 written */
    CHECK_EQ(readByte(0xFF41) & 0x03, ppu.mode); /* mode bits not writable */
    writeByte(0xFF44, 0x55);                     /* LY is read only */
    CHECK_EQ(readByte(0xFF44), 0);
}

static void lcdOnOff(void) {
    setup();
    ppuStep(456 * 5);
    CHECK_EQ(ppu.ly, 5);
    writeByte(0xFF40, 0x11);                   /* LCD off */
    CHECK_EQ(readByte(0xFF44), 0);
    CHECK_EQ(readByte(0xFF41) & 3, 0);
    ppuStep(10000);
    CHECK_EQ(ppu.ly, 0);                       /* frozen while off */
    writeByte(0xFF40, 0x91);                   /* on again */
    CHECK_EQ(ppu.mode, PPU_MODE_OAM);
    ppuStep(456);
    CHECK_EQ(ppu.ly, 1);
}

/* ---- Background ------------------------------------------------------- */

static void backgroundTile(void) {
    setup();
    tileRow(16, 0, 0xFF, 1);
    tileRow(16, 1, 0xFF, 2);
    tileRow(16, 2, 0xF0, 3);
    vram[0x1800] = 1;                          /* map (0,0) = tile 1 */

    renderLine(0);
    CHECK_EQ(ppuBackBuffer[0][0], 1);
    CHECK_EQ(ppuBackBuffer[0][7], 1);
    CHECK_EQ(ppuBackBuffer[0][8], 0);          /* tile 0 is blank */
    renderLine(1);
    CHECK_EQ(ppuBackBuffer[1][3], 2);
    renderLine(2);
    CHECK_EQ(ppuBackBuffer[2][3], 3);
    CHECK_EQ(ppuBackBuffer[2][4], 0);

    ppu.bgp = 0x1B;                            /* inverted palette */
    renderLine(0);
    CHECK_EQ(ppuBackBuffer[0][0], 2);
    CHECK_EQ(ppuBackBuffer[0][8], 3);
}

static void backgroundScroll(void) {
    setup();
    tileRow(16, 0, 0xFF, 1);
    tileRow(16, 5, 0xFF, 3);
    vram[0x1800] = 1;
    ppu.scx = 4;
    renderLine(0);
    CHECK_EQ(ppuBackBuffer[0][3], 1);
    CHECK_EQ(ppuBackBuffer[0][4], 0);
    ppu.scy = 5;
    renderLine(0);
    CHECK_EQ(ppuBackBuffer[0][0], 3);

    /* Wrap around: SCX = 252 shows the last map column, then column 0 */
    ppu.scy = 0;
    ppu.scx = 252;
    renderLine(0);
    CHECK_EQ(ppuBackBuffer[0][3], 0);
    CHECK_EQ(ppuBackBuffer[0][4], 1);
}

static void signedTileAddressing(void) {
    setup();
    ppu.lcdc &= (uint8_t)~LCDC_TILE_DATA;      /* 8800 mode */
    tileRow(0x1000, 0, 0xFF, 3);               /* tile 0 lives at 9000 */
    tileRow(0x0800, 0, 0xFF, 2);               /* tile 0x80 (-128) at 8800 */
    vram[0x1800] = 0x00;
    vram[0x1801] = 0x80;
    renderLine(0);
    CHECK_EQ(ppuBackBuffer[0][0], 3);
    CHECK_EQ(ppuBackBuffer[0][8], 2);
}

static void alternateBackgroundMap(void) {
    setup();
    tileRow(16, 0, 0xFF, 2);
    vram[0x1C00] = 1;                          /* 9C00 map */
    renderLine(0);
    CHECK_EQ(ppuBackBuffer[0][0], 0);
    ppu.lcdc |= LCDC_BG_MAP;
    renderLine(0);
    CHECK_EQ(ppuBackBuffer[0][0], 2);
}

static void window(void) {
    setup();
    tileRow(32, 0, 0xFF, 3);
    vram[0x1C00] = 2;                          /* window map (9C00) tile 2 */
    ppu.lcdc |= LCDC_WINDOW_ENABLE | LCDC_WINDOW_MAP;
    ppu.wx = 7 + 80;
    ppu.wy = 0;
    ppu.windowLine = 0;
    renderLine(0);
    CHECK_EQ(ppuBackBuffer[0][79], 0);
    CHECK_EQ(ppuBackBuffer[0][80], 3);
    CHECK_EQ(ppuBackBuffer[0][87], 3);
    CHECK_EQ(ppu.windowLine, 1);               /* internal line counter */

    ppu.wy = 50;                               /* below this line: no window */
    ppu.windowLine = 0;
    renderLine(10);
    CHECK_EQ(ppuBackBuffer[10][80], 0);
    CHECK_EQ(ppu.windowLine, 0);
}

static void backgroundDisabled(void) {
    setup();
    tileRow(16, 0, 0xFF, 3);
    vram[0x1800] = 1;
    ppu.lcdc &= (uint8_t)~LCDC_BG_ENABLE;
    renderLine(0);
    CHECK_EQ(ppuBackBuffer[0][0], 0);
}

/* ---- Sprites ---------------------------------------------------------- */

static void setSprite(int index, int y, int x, uint8_t tile, uint8_t attributes) {
    oam[index * 4] = (uint8_t)y;
    oam[index * 4 + 1] = (uint8_t)x;
    oam[index * 4 + 2] = tile;
    oam[index * 4 + 3] = attributes;
}

static void spriteBasic(void) {
    setup();
    ppu.lcdc |= LCDC_OBJ_ENABLE;
    tileRow(16, 0, 0xFF, 1);
    setSprite(0, 16, 8, 1, 0);                 /* top-left corner of the screen */
    renderLine(0);
    CHECK_EQ(ppuBackBuffer[0][0], 1);
    CHECK_EQ(ppuBackBuffer[0][7], 1);
    CHECK_EQ(ppuBackBuffer[0][8], 0);

    ppu.lcdc &= (uint8_t)~LCDC_OBJ_ENABLE;     /* sprites off */
    renderLine(0);
    CHECK_EQ(ppuBackBuffer[0][0], 0);
}

static void spriteTransparencyAndFlip(void) {
    setup();
    ppu.lcdc |= LCDC_OBJ_ENABLE;
    tileRow(16, 0, 0xFF, 2);                   /* background: colour 2 */
    vram[0x1800] = 1;
    tileRow(48, 0, 0x0F, 1);                   /* sprite tile 3: right half only */
    setSprite(0, 16, 8, 3, 0);
    renderLine(0);
    CHECK_EQ(ppuBackBuffer[0][0], 2);          /* transparent -> background */
    CHECK_EQ(ppuBackBuffer[0][4], 1);

    setSprite(0, 16, 8, 3, 0x20);              /* X flip */
    renderLine(0);
    CHECK_EQ(ppuBackBuffer[0][0], 1);
    CHECK_EQ(ppuBackBuffer[0][4], 2);

    tileRow(48, 7, 0xFF, 3);
    setSprite(0, 16, 8, 3, 0x40);              /* Y flip: row 7 shown on line 0 */
    renderLine(0);
    CHECK_EQ(ppuBackBuffer[0][0], 3);
}

static void spritePaletteAndPriority(void) {
    setup();
    ppu.lcdc |= LCDC_OBJ_ENABLE;
    ppu.obp1 = 0x1B;
    tileRow(16, 0, 0xFF, 1);
    setSprite(0, 16, 8, 1, 0x10);              /* OBP1 */
    renderLine(0);
    CHECK_EQ(ppuBackBuffer[0][0], 2);

    tileRow(32, 0, 0xF0, 3);                   /* bg tile 2: left half colour 3 */
    vram[0x1800] = 2;
    setSprite(0, 16, 8, 1, 0x80);              /* behind background */
    renderLine(0);
    CHECK_EQ(ppuBackBuffer[0][0], 3);          /* BG colour 1-3 wins */
    CHECK_EQ(ppuBackBuffer[0][4], 1);          /* BG colour 0: sprite shows */
}

static void spriteOrdering(void) {
    setup();
    ppu.lcdc |= LCDC_OBJ_ENABLE;
    tileRow(16, 0, 0xFF, 1);
    tileRow(32, 0, 0xFF, 3);
    setSprite(0, 16, 8, 1, 0);
    setSprite(1, 16, 8, 2, 0);                 /* same X: lower OAM index wins */
    renderLine(0);
    CHECK_EQ(ppuBackBuffer[0][0], 1);

    setSprite(0, 16, 12, 1, 0);                /* OAM 0 further right */
    renderLine(0);
    CHECK_EQ(ppuBackBuffer[0][4], 3);          /* smaller X wins on the DMG */
}

static void spriteLimitPerLine(void) {
    int i;
    setup();
    ppu.lcdc |= LCDC_OBJ_ENABLE;
    tileRow(16, 0, 0xFF, 1);
    for (i = 0; i < 11; i++) setSprite(i, 16, 8 + i * 8, 1, 0);
    renderLine(0);
    CHECK_EQ(ppuBackBuffer[0][9 * 8], 1);      /* 10th sprite drawn */
    CHECK_EQ(ppuBackBuffer[0][10 * 8], 0);     /* 11th sprite dropped */
}

static void tallSprites(void) {
    setup();
    ppu.lcdc |= LCDC_OBJ_ENABLE | LCDC_OBJ_SIZE;
    tileRow(4 * 16, 0, 0xFF, 1);               /* tile 4: top half */
    tileRow(5 * 16, 0, 0xFF, 3);               /* tile 5: bottom half */
    setSprite(0, 16, 8, 5, 0);                 /* bit 0 ignored -> tiles 4,5 */
    renderLine(0);
    CHECK_EQ(ppuBackBuffer[0][0], 1);
    renderLine(8);
    CHECK_EQ(ppuBackBuffer[8][0], 3);
    renderLine(16);
    CHECK_EQ(ppuBackBuffer[16][0], 0);         /* below the sprite */
}

static void frameBufferPublishedAtVBlank(void) {
    setup();
    tileRow(16, 0, 0xFF, 3);
    vram[0x1800] = 1;
    while (ppu.ly < 144) ppuStep(4);
    CHECK_EQ(ppuFrontBuffer[0][0], 3);
    CHECK_EQ(ppuFrontBuffer[0][8], 0);
}

void suitePpu(void) {
    static const struct testCase tests[] = {
        TEST(modeSequence),
        TEST(frameTiming),
        TEST(largeStepsStayInSync),
        TEST(lycCoincidence),
        TEST(statModeInterrupts),
        TEST(statWritableBits),
        TEST(lcdOnOff),
        TEST(backgroundTile),
        TEST(backgroundScroll),
        TEST(signedTileAddressing),
        TEST(alternateBackgroundMap),
        TEST(window),
        TEST(backgroundDisabled),
        TEST(spriteBasic),
        TEST(spriteTransparencyAndFlip),
        TEST(spritePaletteAndPriority),
        TEST(spriteOrdering),
        TEST(spriteLimitPerLine),
        TEST(tallSprites),
        TEST(frameBufferPublishedAtVBlank),
    };
    runTests("ppu", tests, sizeof(tests) / sizeof(tests[0]));
}
