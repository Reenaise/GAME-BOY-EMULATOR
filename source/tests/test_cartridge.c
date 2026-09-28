/* Cartridge tests: loading, header parsing, error handling, MBC1/MBC3/MBC5. */

#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "cartridge.h"
#include "memory.h"
#include "test.h"

/* Build a ROM whose every bank starts with its own bank number. */
static uint8_t *makeBankedRom(size_t banks, uint8_t type, uint8_t romCode, uint8_t ramCode) {
    size_t size = banks * ROM_BANK_SIZE;
    uint8_t *rom = malloc(size);
    size_t b;
    testBuildRom(rom, size, type, romCode, ramCode);
    for (b = 1; b < banks; b++) {
        rom[b * ROM_BANK_SIZE] = (uint8_t)b;
        rom[b * ROM_BANK_SIZE + 1] = (uint8_t)(b >> 8);
        rom[b * ROM_BANK_SIZE + 0x3FFF] = (uint8_t)(b ^ 0xFF);
    }
    rom[0x0000] = 0x00; /* bank 0 marker */
    return rom;
}

static void headerParsing(void) {
    uint8_t *rom = makeBankedRom(2, 0x00, 0x00, 0x00);
    struct cartridgeHeader header;
    CHECK_EQ(cartridgeParseHeader(rom, 0x8000, &header), CART_OK);
    CHECK(strcmp(header.title, "TESTROM") == 0);
    CHECK_EQ(header.type, 0x00);
    CHECK(header.logoValid);
    CHECK(header.headerChecksumValid);

    rom[HEADER_TITLE] = 'X';                   /* breaks the checksum only */
    CHECK_EQ(cartridgeParseHeader(rom, 0x8000, &header), CART_OK);
    CHECK(!header.headerChecksumValid);
    free(rom);
}

static void loadRomOnly(void) {
    uint8_t *rom = makeBankedRom(2, 0x00, 0x00, 0x00);
    CHECK_EQ(cartridgeLoadBuffer(rom, 0x8000), CART_OK);
    CHECK_EQ(cartridge.mbc, MBC_NONE);
    CHECK_EQ(cartridge.romBanks, 2);
    CHECK_EQ(cartridgeRead(0x0101), 0xC3);
    CHECK_EQ(cartridgeRead(0x4000), 1);
    cartridgeWrite(0x2000, 0x05);              /* no MBC: ignored */
    CHECK_EQ(cartridgeRead(0x4000), 1);
    free(rom);
}

static void rejectBadFiles(void) {
    static uint8_t garbage[0x8000];
    uint8_t *rom;
    size_t i;

    for (i = 0; i < sizeof(garbage); i++) garbage[i] = (uint8_t)(i * 7 + 3);
    CHECK_EQ(cartridgeLoadBuffer(garbage, 0x100), CART_ERR_TOO_SMALL);
    CHECK_EQ(cartridgeLoadBuffer(garbage, sizeof(garbage)), CART_ERR_INVALID);

    rom = makeBankedRom(2, 0x06, 0x00, 0x00);  /* MBC2+BATTERY */
    /* fix up the checksum for the new type */
    {
        uint8_t checksum = 0;
        int a;
        for (a = HEADER_TITLE; a <= HEADER_VERSION; a++) checksum = (uint8_t)(checksum - rom[a] - 1);
        rom[HEADER_CHECKSUM] = checksum;
    }
    CHECK_EQ(cartridgeLoadBuffer(rom, 0x8000), CART_ERR_UNSUPPORTED);
    CHECK_EQ(cartridge.header.type, 0x06);
    CHECK(!cartridge.loaded);

    rom[HEADER_TYPE] = 0x00;
    rom[HEADER_ROM_SIZE] = 0x42;               /* corrupted size code */
    CHECK_EQ(cartridgeLoadBuffer(rom, 0x8000), CART_ERR_BAD_HEADER);
    free(rom);

    CHECK_EQ(cartridgeLoadFile("this/file/does/not/exist.gb"), CART_ERR_OPEN);
    CHECK(cartridgeErrorString(CART_ERR_UNSUPPORTED)[0] != '\0');
}

static void loadFromFile(void) {
    const char *path = "test_rom_tmp.gb";
    uint8_t *rom = makeBankedRom(4, 0x01, 0x01, 0x00);
    FILE *file = fopen(path, "wb");
    CHECK(file != NULL);
    if (!file) { free(rom); return; }
    fwrite(rom, 1, 4 * ROM_BANK_SIZE, file);
    fclose(file);

    CHECK_EQ(cartridgeLoadFile(path), CART_OK);
    CHECK_EQ(cartridge.mbc, MBC_1);
    CHECK_EQ(cartridge.romBanks, 4);
    CHECK(strcmp(cartridge.savePath, "test_rom_tmp.sav") == 0);
    remove(path);
    free(rom);
}

static void shortRomIsPadded(void) {
    uint8_t *rom = makeBankedRom(2, 0x00, 0x00, 0x00);
    CHECK_EQ(cartridgeLoadBuffer(rom, 0x5000), CART_OK); /* trimmed file */
    CHECK_EQ(cartridge.romSize, 0x8000);
    CHECK_EQ(cartridgeRead(0x4000), 1);
    CHECK_EQ(cartridgeRead(0x7FFF), 0xFF);
    free(rom);
}

static void mbc1RomBanking(void) {
    uint8_t *rom = makeBankedRom(32, 0x01, 0x04, 0x00); /* 512 KiB */
    CHECK_EQ(cartridgeLoadBuffer(rom, 32 * ROM_BANK_SIZE), CART_OK);

    CHECK_EQ(cartridgeRead(0x4000), 1);        /* bank 1 by default */
    cartridgeWrite(0x2000, 5);
    CHECK_EQ(cartridgeRead(0x4000), 5);
    CHECK_EQ(cartridgeRead(0x7FFF), 5 ^ 0xFF);
    cartridgeWrite(0x3FFF, 0x1F);
    CHECK_EQ(cartridgeRead(0x4000), 31);
    cartridgeWrite(0x2000, 0);                 /* 0 selects bank 1 */
    CHECK_EQ(cartridgeRead(0x4000), 1);
    cartridgeWrite(0x2000, 0xE3);              /* only 5 bits used */
    CHECK_EQ(cartridgeRead(0x4000), 3);
    CHECK_EQ(cartridgeRead(0x0000), 0);        /* bank 0 fixed */
    free(rom);
}

static void mbc1LargeRom(void) {
    uint8_t *rom = makeBankedRom(64, 0x01, 0x05, 0x00); /* 1 MiB */
    CHECK_EQ(cartridgeLoadBuffer(rom, 64 * ROM_BANK_SIZE), CART_OK);

    cartridgeWrite(0x4000, 1);                 /* BANK2 = 1 */
    cartridgeWrite(0x2000, 2);
    CHECK_EQ(cartridgeRead(0x4000), 34);       /* (1 << 5) | 2 */
    cartridgeWrite(0x2000, 0);                 /* bank 0x20 -> 0x21 */
    CHECK_EQ(cartridgeRead(0x4000), 33);
    CHECK_EQ(cartridgeRead(0x0000), 0);        /* mode 0: low area = bank 0 */
    cartridgeWrite(0x6000, 1);                 /* mode 1 */
    CHECK_EQ(cartridgeRead(0x0000), 32);       /* low area = bank 0x20 */
    cartridgeWrite(0x6000, 0);
    CHECK_EQ(cartridgeRead(0x0000), 0);
    free(rom);
}

static void mbc1BankMasking(void) {
    uint8_t *rom = makeBankedRom(4, 0x01, 0x01, 0x00); /* 64 KiB */
    CHECK_EQ(cartridgeLoadBuffer(rom, 4 * ROM_BANK_SIZE), CART_OK);
    cartridgeWrite(0x2000, 5);                 /* only 2 bank bits exist */
    CHECK_EQ(cartridgeRead(0x4000), 1);
    cartridgeWrite(0x2000, 7);
    CHECK_EQ(cartridgeRead(0x4000), 3);
    free(rom);
}

static void mbc1Ram(void) {
    uint8_t *rom = makeBankedRom(4, 0x03, 0x01, 0x03); /* MBC1+RAM+BATTERY, 32 KiB RAM */
    CHECK_EQ(cartridgeLoadBuffer(rom, 4 * ROM_BANK_SIZE), CART_OK);
    CHECK(cartridge.hasBattery);
    CHECK_EQ(cartridge.ramSize, 32 * 1024);
    CHECK_EQ(cartridge.ramBanks, 4);

    cartridgeWrite(0xA000, 0x12);              /* disabled: ignored */
    CHECK_EQ(cartridgeRead(0xA000), 0xFF);
    cartridgeWrite(0x0000, 0x0A);              /* enable */
    cartridgeWrite(0xA000, 0x12);
    CHECK_EQ(cartridgeRead(0xA000), 0x12);
    CHECK_EQ(readByte(0xA000), 0x12);          /* through the bus */

    cartridgeWrite(0x6000, 1);                 /* mode 1: BANK2 selects RAM bank */
    cartridgeWrite(0x4000, 2);
    CHECK_EQ(cartridgeRead(0xA000), 0xFF);     /* fresh bank */
    cartridgeWrite(0xBFFF, 0x34);
    CHECK_EQ(cartridge.ram[2 * RAM_BANK_SIZE + 0x1FFF], 0x34);
    cartridgeWrite(0x4000, 0);
    CHECK_EQ(cartridgeRead(0xA000), 0x12);

    cartridgeWrite(0x6000, 0);                 /* mode 0: always RAM bank 0 */
    cartridgeWrite(0x4000, 2);
    CHECK_EQ(cartridgeRead(0xA000), 0x12);

    cartridgeWrite(0x0000, 0x00);              /* disable again */
    CHECK_EQ(cartridgeRead(0xA000), 0xFF);
    free(rom);
}

static void romOnlyWithRam(void) {
    uint8_t *rom = makeBankedRom(2, 0x08, 0x00, 0x02);
    CHECK_EQ(cartridgeLoadBuffer(rom, 0x8000), CART_OK);
    cartridgeWrite(0xA123, 0x77);              /* no MBC: RAM always on */
    CHECK_EQ(cartridgeRead(0xA123), 0x77);
    free(rom);
}

/* ---- MBC3 ----------------------------------------------------------------- */

static void mbc3RomBanking(void) {
    uint8_t *rom = makeBankedRom(128, 0x11, 0x06, 0x00); /* MBC3, 2 MiB */
    CHECK_EQ(cartridgeLoadBuffer(rom, 128 * ROM_BANK_SIZE), CART_OK);
    CHECK_EQ(cartridge.mbc, MBC_3);
    CHECK_EQ(cartridgeRead(0x4000), 1);
    cartridgeWrite(0x2000, 0x7F);                 /* 7-bit bank number */
    CHECK_EQ(cartridgeRead(0x4000), 0x7F);
    cartridgeWrite(0x2000, 0x20);                 /* no MBC1 0x20 -> 0x21 quirk */
    CHECK_EQ(cartridgeRead(0x4000), 0x20);
    cartridgeWrite(0x2000, 0x00);                 /* 0 still means 1 */
    CHECK_EQ(cartridgeRead(0x4000), 1);
    CHECK_EQ(cartridgeRead(0x0000), 0);           /* bank 0 always fixed */
    free(rom);
}

static void mbc3Ram(void) {
    uint8_t *rom = makeBankedRom(4, 0x13, 0x01, 0x03); /* MBC3+RAM+BATTERY, 32 KiB */
    int bank;
    CHECK_EQ(cartridgeLoadBuffer(rom, 4 * ROM_BANK_SIZE), CART_OK);
    CHECK_EQ(cartridge.ramBanks, 4);
    CHECK(!cartridge.hasRtc);
    cartridgeWrite(0xA000, 0x55);                 /* disabled */
    CHECK_EQ(cartridgeRead(0xA000), 0xFF);
    cartridgeWrite(0x0000, 0x0A);
    for (bank = 0; bank < 4; bank++) {
        cartridgeWrite(0x4000, (uint8_t)bank);
        cartridgeWrite(0xA010, (uint8_t)(0x40 + bank));
    }
    for (bank = 0; bank < 4; bank++) {
        cartridgeWrite(0x4000, (uint8_t)bank);
        CHECK_EQ(cartridgeRead(0xA010), 0x40 + bank);
    }
    cartridgeWrite(0x4000, 0x08);                 /* no clock on this cart */
    CHECK_EQ(cartridgeRead(0xA000), 0xFF);
    free(rom);
}

static void latch(void) {
    cartridgeWrite(0x6000, 0x00);
    cartridgeWrite(0x6000, 0x01);
}

static uint8_t readRtc(uint8_t reg) {
    cartridgeWrite(0x4000, reg);
    return cartridgeRead(0xA000);
}

static void mbc3RealTimeClock(void) {
    uint8_t *rom = makeBankedRom(4, 0x10, 0x01, 0x03); /* MBC3+TIMER+RAM+BATTERY */
    int64_t now = (int64_t)time(NULL);
    uint8_t seconds;

    CHECK_EQ(cartridgeLoadBuffer(rom, 4 * ROM_BANK_SIZE), CART_OK);
    CHECK(cartridge.hasRtc);
    cartridgeWrite(0x0000, 0x0A);

    /* Pretend the clock was started 1 h 2 min 5 s ago. */
    memset(&cartridge.rtc, 0, sizeof(cartridge.rtc));
    cartridge.rtc.latchWrite = 0xFF;
    cartridge.rtc.lastUpdate = now - 3725;
    latch();
    seconds = readRtc(0x08);
    CHECK(seconds >= 5 && seconds <= 6);          /* allow a second tick */
    CHECK_EQ(readRtc(0x09), 2);
    CHECK_EQ(readRtc(0x0A), 1);
    CHECK_EQ(readRtc(0x0B), 0);

    /* The latched copy does not move until the next 0 -> 1 latch. */
    cartridge.rtc.lastUpdate -= 60;
    CHECK_EQ(readRtc(0x09), 2);
    latch();
    CHECK_EQ(readRtc(0x09), 3);

    /* Halt bit stops the clock. */
    cartridgeWrite(0x4000, 0x0C);
    cartridgeWrite(0xA000, 0x40);
    cartridge.rtc.lastUpdate -= 600;
    latch();
    CHECK_EQ(readRtc(0x09), 3);
    CHECK_EQ(readRtc(0x0C), 0x40);

    /* Day counter: 511 + 1 day overflows to 0 and sets the carry bit. */
    cartridgeWrite(0x4000, 0x0C);
    cartridgeWrite(0xA000, 0x01);                 /* running, day bit 8 */
    cartridgeWrite(0x4000, 0x0B);
    cartridgeWrite(0xA000, 0xFF);                 /* day 511 */
    cartridge.rtc.lastUpdate -= 86400;
    latch();
    CHECK_EQ(readRtc(0x0B), 0);
    CHECK_EQ(readRtc(0x0C), 0x80);

    /* Written values are masked to the register width. */
    cartridgeWrite(0x4000, 0x0A);
    cartridgeWrite(0xA000, 0xFF);
    CHECK_EQ(readRtc(0x0A), 0x1F);
    free(rom);
}

static void mbc3SaveFileWithClock(void) {
    const char *romPath = "test_rtc_tmp.gb";
    const char *savPath = "test_rtc_tmp.sav";
    uint8_t *rom = makeBankedRom(4, 0x10, 0x01, 0x02); /* 8 KiB RAM + clock */
    FILE *file = fopen(romPath, "wb");
    long size;

    CHECK(file != NULL);
    if (!file) { free(rom); return; }
    fwrite(rom, 1, 4 * ROM_BANK_SIZE, file);
    fclose(file);
    remove(savPath);

    CHECK_EQ(cartridgeLoadFile(romPath), CART_OK);
    cartridgeWrite(0x0000, 0x0A);
    cartridgeWrite(0x4000, 0x00);
    cartridgeWrite(0xA123, 0x99);
    cartridgeWrite(0x4000, 0x0A);                 /* hours = 7 */
    cartridgeWrite(0xA000, 7);
    CHECK(cartridgeSaveRam());

    file = fopen(savPath, "rb");
    CHECK(file != NULL);
    if (file) {
        fseek(file, 0, SEEK_END);
        size = ftell(file);
        fclose(file);
        CHECK_EQ(size, 8192 + 48);                /* RAM + BGB/VBA clock footer */
    }

    CHECK_EQ(cartridgeLoadFile(romPath), CART_OK); /* loads the .sav again */
    CHECK_EQ(cartridge.ram[0x123], 0x99);
    CHECK_EQ(cartridge.rtc.hours, 7);
    remove(romPath);
    remove(savPath);
    free(rom);
}

/* ---- MBC5 ----------------------------------------------------------------- */

static void mbc5RomBanking(void) {
    uint8_t *rom = makeBankedRom(512, 0x19, 0x08, 0x00); /* MBC5, 8 MiB */
    CHECK_EQ(cartridgeLoadBuffer(rom, 512 * ROM_BANK_SIZE), CART_OK);
    CHECK_EQ(cartridge.mbc, MBC_5);
    CHECK_EQ(cartridge.romBanks, 512);
    CHECK_EQ(cartridgeRead(0x4000), 1);
    cartridgeWrite(0x2000, 0x00);                 /* bank 0 really is bank 0 on MBC5 */
    CHECK_EQ(cartridgeRead(0x4000), 0);
    cartridgeWrite(0x2000, 0x23);
    cartridgeWrite(0x3000, 0x01);                 /* 9th bit -> bank 0x123 */
    CHECK_EQ(cartridgeRead(0x4000), 0x23);
    CHECK_EQ(cartridgeRead(0x4001), 0x01);
    cartridgeWrite(0x2000, 0xFF);                 /* low byte only */
    CHECK_EQ(cartridgeRead(0x4000), 0xFF);
    CHECK_EQ(cartridgeRead(0x4001), 0x01);        /* bank 0x1FF */
    cartridgeWrite(0x3000, 0x00);
    CHECK_EQ(cartridgeRead(0x4001), 0x00);        /* bank 0x0FF */
    free(rom);
}

static void mbc5Ram(void) {
    uint8_t *rom = makeBankedRom(4, 0x1B, 0x01, 0x04); /* MBC5+RAM+BATTERY, 128 KiB */
    int bank;
    CHECK_EQ(cartridgeLoadBuffer(rom, 4 * ROM_BANK_SIZE), CART_OK);
    CHECK_EQ(cartridge.ramBanks, 16);
    cartridgeWrite(0x0000, 0x0A);
    for (bank = 0; bank < 16; bank++) {
        cartridgeWrite(0x4000, (uint8_t)bank);
        cartridgeWrite(0xBFFF, (uint8_t)(0xB0 + bank));
    }
    for (bank = 0; bank < 16; bank++) {
        cartridgeWrite(0x4000, (uint8_t)bank);
        CHECK_EQ(cartridgeRead(0xBFFF), 0xB0 + bank);
    }
    cartridgeWrite(0x0000, 0x00);
    CHECK_EQ(cartridgeRead(0xBFFF), 0xFF);
    free(rom);
}

static void mbc5RumbleBitIgnored(void) {
    uint8_t *rom = makeBankedRom(4, 0x1E, 0x01, 0x03); /* MBC5+RUMBLE+RAM+BATTERY */
    CHECK_EQ(cartridgeLoadBuffer(rom, 4 * ROM_BANK_SIZE), CART_OK);
    CHECK(cartridge.hasRumble);
    cartridgeWrite(0x0000, 0x0A);
    cartridgeWrite(0x4000, 0x01);
    cartridgeWrite(0xA000, 0x11);
    cartridgeWrite(0x4000, 0x09);                 /* motor on + bank 1 */
    CHECK_EQ(cartridgeRead(0xA000), 0x11);
    free(rom);
}

static void typeNames(void) {
    CHECK(strcmp(cartridgeTypeName(0x00), "ROM ONLY") == 0);
    CHECK(strcmp(cartridgeTypeName(0x03), "MBC1+RAM+BATTERY") == 0);
    CHECK(cartridgeTypeSupported(0x01));
    CHECK(cartridgeTypeSupported(0x13));
    CHECK(cartridgeTypeSupported(0x19));
    CHECK(!cartridgeTypeSupported(0x05)); /* MBC2 */
    CHECK(!cartridgeTypeSupported(0x22)); /* MBC7 */
}

void suiteCartridge(void) {
    static const struct testCase tests[] = {
        TEST(headerParsing),
        TEST(loadRomOnly),
        TEST(rejectBadFiles),
        TEST(loadFromFile),
        TEST(shortRomIsPadded),
        TEST(mbc1RomBanking),
        TEST(mbc1LargeRom),
        TEST(mbc1BankMasking),
        TEST(mbc1Ram),
        TEST(romOnlyWithRam),
        TEST(mbc3RomBanking),
        TEST(mbc3Ram),
        TEST(mbc3RealTimeClock),
        TEST(mbc3SaveFileWithClock),
        TEST(mbc5RomBanking),
        TEST(mbc5Ram),
        TEST(mbc5RumbleBitIgnored),
        TEST(typeNames),
    };
    runTests("cartridge", tests, sizeof(tests) / sizeof(tests[0]));
    cartridgeUnload();
}
