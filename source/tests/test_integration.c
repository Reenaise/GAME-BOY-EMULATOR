/*
 * Integration tests: whole programs running through emulatorRunFrame(),
 * i.e. CPU + memory + cartridge + timer + PPU + interrupts together.
 */

#include <stdio.h>
#include <string.h>

#include "cartridge.h"
#include "cpu.h"
#include "emulator.h"
#include "input.h"
#include "memory.h"
#include "ppu.h"
#include "serial.h"
#include "test.h"

static bool loadRom(const char *name) {
    char path[512];
    cartError error;
    snprintf(path, sizeof(path), "%s/%s", GB_ROMS_DIR, name);
    error = emulatorLoad(path, NULL);
    if (error != CART_OK) {
        printf("    cannot load %s: %s\n", path, cartridgeErrorString(error));
        return false;
    }
    return true;
}

static void runFrames(int count) {
    while (count-- > 0) emulatorRunFrame();
}

/* A program assembled here: a V-Blank handler counts frames in HRAM. */
static void vblankCounterProgram(void) {
    static uint8_t rom[0x8000];
    static const uint8_t handler[] = {
        0xF5,                   /* PUSH AF          */
        0xF0, 0x80,             /* LDH A,(80)       */
        0x3C,                   /* INC A            */
        0xE0, 0x80,             /* LDH (80),A       */
        0xF1,                   /* POP AF           */
        0xD9,                   /* RETI             */
    };
    static const uint8_t program[] = {
        0xAF, 0xE0, 0x80,       /* XOR A ; LDH (80),A     */
        0x3E, 0x01, 0xE0, 0xFF, /* LD A,1 ; LDH (IE),A    */
        0xAF, 0xE0, 0x0F,       /* XOR A ; LDH (IF),A     */
        0xFB,                   /* EI                     */
        0x76, 0x00,             /* loop: HALT ; NOP       */
        0x18, 0xFC,             /* JR loop                */
    };
    testBuildRom(rom, sizeof(rom), 0x00, 0x00, 0x00);
    memcpy(rom + 0x40, handler, sizeof(handler));
    memcpy(rom + 0x150, program, sizeof(program));
    CHECK_EQ(cartridgeLoadBuffer(rom, sizeof(rom)), CART_OK);
    emulatorReset();

    runFrames(60);
    /* The first frame may start part-way through, so allow one either way. */
    CHECK(hram[0] >= 59 && hram[0] <= 61);
    CHECK(!cpu.locked);
    CHECK_EQ(ppu.frames, 60);
}

static void mbc1TestRom(void) {
    FILE *capture;
    char output[64] = { 0 };
    size_t length;

    if (!loadRom("mbc1_test.gb")) { CHECK(false); return; }
    CHECK_EQ(cartridge.mbc, MBC_1);
    CHECK_EQ(cartridge.romBanks, 8);
    CHECK_EQ(cartridge.ramBanks, 4);

    capture = tmpfile();
    serialSetOutput(capture);
    runFrames(30);
    serialSetOutput(NULL);

    CHECK_EQ(readByte(0xC000), 0x01);          /* the ROM's own verdict */
    if (capture) {
        rewind(capture);
        length = fread(output, 1, sizeof(output) - 1, capture);
        output[length] = '\0';
        fclose(capture);
        CHECK(strcmp(output, "MBC1 PASS\n") == 0);
    }
}

static int countShade(int shade) {
    int x, y, count = 0;
    for (y = 0; y < SCREEN_HEIGHT; y++)
        for (x = 0; x < SCREEN_WIDTH; x++)
            if (ppuFrontBuffer[y][x] == shade) count++;
    return count;
}

static void demoRom(void) {
    uint8_t startX, startY;

    if (!loadRom("demo.gb")) { CHECK(false); return; }
    runFrames(10);
    CHECK(!cpu.locked);
    CHECK_EQ(readByte(0xC106), 0x01);          /* MBC1 self-check passed */
    CHECK_EQ(ppu.lcdc, 0xF3);
    CHECK(countShade(3) > 200);                /* text is drawn */
    CHECK(countShade(0) > 10000);              /* mostly background */

    /* The smiley sprite: OAM y=80 x=84 -> screen (76, 64) */
    CHECK_EQ(oam[0], 80);
    CHECK_EQ(oam[1], 84);
    CHECK_EQ(ppuFrontBuffer[64 + 3][76 + 3], 1); /* face colour through OBP0 */

    /* Hold Right for 20 frames: the sprite moves 1 pixel per frame */
    startX = readByte(0xC001);
    startY = readByte(0xC000);
    inputSetButton(BUTTON_RIGHT, true);
    runFrames(20);
    inputSetButton(BUTTON_RIGHT, false);
    CHECK(readByte(0xC001) >= startX + 18 && readByte(0xC001) <= startX + 21);
    CHECK_EQ(readByte(0xC000), startY);

    inputSetButton(BUTTON_DOWN, true);
    runFrames(5);
    inputSetButton(BUTTON_DOWN, false);
    CHECK(readByte(0xC000) > startY);

    /* A inverts the background palette */
    runFrames(2);
    inputSetButton(BUTTON_A, true);
    runFrames(3);
    inputSetButton(BUTTON_A, false);
    runFrames(2);
    CHECK_EQ(ppu.bgp, 0x1B);
    CHECK(countShade(3) > 10000);              /* background now dark */

    /* Timer interrupt drives the seconds counter: 3 s of emulated time */
    runFrames(180);
    CHECK(readByte(0xC102) >= '2' && readByte(0xC102) <= '4');
}

void suiteIntegration(void) {
    static const struct testCase tests[] = {
        TEST(vblankCounterProgram),
        TEST(mbc1TestRom),
        TEST(demoRom),
    };
    runTests("integration", tests, sizeof(tests) / sizeof(tests[0]));
    cartridgeUnload();
}
