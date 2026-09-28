/* Memory bus tests: address decoding, mirrors, I/O registers, DMA. */

#include "cartridge.h"
#include "input.h"
#include "interrupts.h"
#include "memory.h"
#include "ppu.h"
#include "test.h"
#include "timer.h"

static void workRamAndEcho(void) {
    testResetMachine();
    writeByte(0xC000, 0x11);
    writeByte(0xDDFF, 0x22);
    CHECK_EQ(readByte(0xC000), 0x11);
    CHECK_EQ(readByte(0xE000), 0x11);   /* echo */
    CHECK_EQ(readByte(0xFDFF), 0x22);
    writeByte(0xE123, 0x33);            /* writing the echo writes WRAM */
    CHECK_EQ(readByte(0xC123), 0x33);
    CHECK_EQ(wram[0x0123], 0x33);
}

static void videoRamAndOam(void) {
    testResetMachine();
    writeByte(0x8000, 0xAA);
    writeByte(0x9FFF, 0xBB);
    CHECK_EQ(vram[0], 0xAA);
    CHECK_EQ(vram[0x1FFF], 0xBB);
    writeByte(0xFE00, 0x10);
    writeByte(0xFE9F, 0x20);
    CHECK_EQ(oam[0], 0x10);
    CHECK_EQ(readByte(0xFE9F), 0x20);
}

static void unusableRegion(void) {
    testResetMachine();
    writeByte(0xFEA0, 0x12);
    CHECK_EQ(readByte(0xFEA0), 0xFF);
    CHECK_EQ(readByte(0xFEFF), 0xFF);
}

static void highRamAndIE(void) {
    testResetMachine();
    writeByte(0xFF80, 0x01);
    writeByte(0xFFFE, 0x02);
    CHECK_EQ(hram[0], 0x01);
    CHECK_EQ(readByte(0xFFFE), 0x02);
    writeByte(0xFFFF, 0x1F);
    CHECK_EQ(interrupt.enable, 0x1F);
    CHECK_EQ(readByte(0xFFFF), 0x1F);
}

static void shortAccess(void) {
    testResetMachine();
    writeShort(0xC010, 0xBEEF);
    CHECK_EQ(readByte(0xC010), 0xEF);   /* little endian */
    CHECK_EQ(readByte(0xC011), 0xBE);
    CHECK_EQ(readShort(0xC010), 0xBEEF);
}

static void romIsReadOnly(void) {
    testResetMachine();
    CHECK_EQ(readByte(0x0101), 0xC3);
    writeByte(0x0101, 0x00);
    CHECK_EQ(readByte(0x0101), 0xC3);
    CHECK_EQ(readByte(0xA000), 0xFF);   /* no cartridge RAM */
}

static void ioRegistersRouted(void) {
    testResetMachine();
    writeByte(0xFF42, 0x12);
    CHECK_EQ(ppu.scy, 0x12);
    CHECK_EQ(readByte(0xFF42), 0x12);
    writeByte(0xFF06, 0x34);
    CHECK_EQ(timer.tma, 0x34);
    writeByte(0xFF0F, 0xFF);
    CHECK_EQ(interrupt.flags, 0x1F);
    CHECK_EQ(readByte(0xFF0F), 0xFF);   /* upper 3 bits read as 1 */
    writeByte(0xFF0F, 0x00);
    CHECK_EQ(readByte(0xFF0F), 0xE0);
    writeByte(0xFF00, 0x20);
    CHECK_EQ(input.select, 0x20);
    CHECK_EQ(readByte(0xFF7F), 0xFF);   /* unmapped */
    CHECK_EQ(readByte(0xFF4D), 0xFF);   /* CGB speed switch: absent on DMG */
}

static void soundRegistersAreSafe(void) {
    uint16_t address;
    testResetMachine();
    /* Sound is not emulated, but every register must accept writes and
     * read back sensibly without affecting anything else. */
    for (address = 0xFF10; address <= 0xFF3F; address++) writeByte(address, 0x00);
    CHECK_EQ(readByte(0xFF10), 0x80);   /* NR10 bit 7 reads 1 */
    CHECK_EQ(readByte(0xFF11), 0x3F);   /* length bits are write-only */
    CHECK_EQ(readByte(0xFF26), 0x70);   /* NR52: powered off, no channels */
    writeByte(0xFF26, 0x80);
    CHECK_EQ(readByte(0xFF26), 0xF0);   /* powered on, channels still off */
    writeByte(0xFF24, 0x77);
    CHECK_EQ(readByte(0xFF24), 0x77);
    writeByte(0xFF30, 0x5A);            /* wave RAM */
    CHECK_EQ(readByte(0xFF30), 0x5A);
    CHECK_EQ(timer.tac, 0xF8);          /* nothing else changed */
    CHECK_EQ(ppu.lcdc, 0x91);
}

static void oamDma(void) {
    int i;
    testResetMachine();
    for (i = 0; i < OAM_SIZE; i++) writeByte((uint16_t)(0xC100 + i), (uint8_t)(i ^ 0x5A));
    writeByte(0xFF46, 0xC1);
    for (i = 0; i < OAM_SIZE; i++) CHECK_EQ(oam[i], (uint8_t)(i ^ 0x5A));
    CHECK_EQ(readByte(0xFF46), 0xC1);
}

static void bootRomOverlay(void) {
    static uint8_t boot[256];
    int i;
    for (i = 0; i < 256; i++) boot[i] = 0x42;
    testResetMachine();
    memorySetBootRom(boot);
    CHECK_EQ(readByte(0x0000), 0x42);
    CHECK_EQ(readByte(0x0100), 0x00);   /* cartridge after 0xFF */
    writeByte(0xFF50, 0x01);            /* boot ROM disables itself */
    CHECK(!memoryBootRomMapped());
    CHECK_EQ(readByte(0x0000), cartridgeRead(0x0000));
    memorySetBootRom(NULL);
}

void suiteMemory(void) {
    static const struct testCase tests[] = {
        TEST(workRamAndEcho),
        TEST(videoRamAndOam),
        TEST(unusableRegion),
        TEST(highRamAndIE),
        TEST(shortAccess),
        TEST(romIsReadOnly),
        TEST(ioRegistersRouted),
        TEST(soundRegistersAreSafe),
        TEST(oamDma),
        TEST(bootRomOverlay),
    };
    runTests("memory", tests, sizeof(tests) / sizeof(tests[0]));
}
