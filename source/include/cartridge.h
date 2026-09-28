#ifndef GB_CARTRIDGE_H
#define GB_CARTRIDGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* Header field offsets (see docs/cartridge.md). */
#define HEADER_ENTRY          0x0100
#define HEADER_LOGO           0x0104
#define HEADER_TITLE          0x0134
#define HEADER_CGB_FLAG       0x0143
#define HEADER_SGB_FLAG       0x0146
#define HEADER_TYPE           0x0147
#define HEADER_ROM_SIZE       0x0148
#define HEADER_RAM_SIZE       0x0149
#define HEADER_DESTINATION    0x014A
#define HEADER_OLD_LICENSEE   0x014B
#define HEADER_VERSION        0x014C
#define HEADER_CHECKSUM       0x014D
#define HEADER_GLOBAL_CHECKSUM 0x014E
#define HEADER_END            0x0150

#define ROM_BANK_SIZE 0x4000
#define RAM_BANK_SIZE 0x2000

enum mbcType {
    MBC_NONE,   /* 32 KiB ROM only (optionally with 8 KiB RAM) */
    MBC_1,
    MBC_3,      /* up to 2 MiB ROM, 32 KiB RAM, optional real-time clock */
    MBC_5,      /* up to 8 MiB ROM, 128 KiB RAM, optional rumble */
};

/* MBC3 real-time clock. Runs on the host's wall clock. */
struct rtc {
    uint8_t seconds, minutes, hours, daysLow, daysHigh; /* live registers   */
    uint8_t latched[5];                                 /* copy read by CPU */
    uint8_t latchWrite;                                 /* last 6000 write  */
    int64_t lastUpdate;                                 /* host time (s)    */
};

typedef enum {
    CART_OK = 0,
    CART_ERR_OPEN,
    CART_ERR_READ,
    CART_ERR_TOO_SMALL,
    CART_ERR_TOO_LARGE,
    CART_ERR_INVALID,
    CART_ERR_BAD_HEADER,
    CART_ERR_UNSUPPORTED,
    CART_ERR_NO_MEMORY,
} cartError;

struct cartridgeHeader {
    char title[17];
    uint8_t cgbFlag;
    uint8_t sgbFlag;
    uint8_t type;
    uint8_t romSizeCode;
    uint8_t ramSizeCode;
    uint8_t destination;
    uint8_t oldLicensee;
    uint8_t version;
    uint8_t headerChecksum;
    uint8_t computedHeaderChecksum;
    uint16_t globalChecksum;
    bool logoValid;
    bool headerChecksumValid;
};

struct cartridge {
    bool loaded;
    struct cartridgeHeader header;
    enum mbcType mbc;
    bool hasRam;
    bool hasBattery;
    bool hasRtc;
    bool hasRumble;

    uint8_t *rom;
    size_t romSize;       /* in bytes, always a multiple of ROM_BANK_SIZE */
    unsigned romBanks;

    uint8_t *ram;
    size_t ramSize;
    unsigned ramBanks;

    /* MBC1 registers */
    bool ramEnabled;
    uint8_t bank1;        /* 5-bit ROM bank register (2000-3FFF)          */
    uint8_t bank2;        /* 2-bit upper bank / RAM bank (4000-5FFF)      */
    uint8_t mode;         /* banking mode select (6000-7FFF)              */

    /* MBC3 / MBC5 registers */
    uint16_t romBank;     /* 4000-7FFF bank (MBC3: 7 bits, MBC5: 9 bits)  */
    uint8_t ramBank;      /* MBC3: 0-3 RAM, 08-0C RTC; MBC5: 0-15         */
    struct rtc rtc;

    char savePath[1024];  /* where battery-backed RAM is stored           */
};

extern struct cartridge cartridge;

cartError cartridgeLoadFile(const char *path);
/* Load from memory (used by tests). The data is copied. */
cartError cartridgeLoadBuffer(const uint8_t *data, size_t size);
void cartridgeUnload(void);
/* Reset the MBC registers (bank 1 selected, RAM disabled). */
void cartridgeResetMbc(void);

const char *cartridgeErrorString(cartError error);
const char *cartridgeTypeName(uint8_t type);
bool cartridgeTypeSupported(uint8_t type);
void cartridgePrintInfo(FILE *out);

uint8_t cartridgeRead(uint16_t address);
void cartridgeWrite(uint16_t address, uint8_t value);

/* Battery-backed RAM (.sav next to the ROM). */
bool cartridgeLoadRam(void);
bool cartridgeSaveRam(void);

/* Advance the MBC3 clock to host time `now` (seconds). Exposed for tests. */
void cartridgeRtcUpdate(int64_t now);

/* Parse a header from a raw ROM image (no state is changed). */
cartError cartridgeParseHeader(const uint8_t *data, size_t size, struct cartridgeHeader *out);

#endif
