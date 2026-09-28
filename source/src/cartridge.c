/*
 * Cartridge: ROM loading, header parsing and the MBC1 memory bank controller.
 *
 * Cinoop's rom.c only loads 32 KiB ROMs without a mapper. MBC1 support is an
 * addition needed for the large majority of early DMG games.
 */

#include "cartridge.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

struct cartridge cartridge;

/* The Nintendo logo every licensed cartridge must contain at 0104-0133. */
static const uint8_t nintendoLogo[48] = {
    0xCE, 0xED, 0x66, 0x66, 0xCC, 0x0D, 0x00, 0x0B, 0x03, 0x73, 0x00, 0x83,
    0x00, 0x0C, 0x00, 0x0D, 0x00, 0x08, 0x11, 0x1F, 0x88, 0x89, 0x00, 0x0E,
    0xDC, 0xCC, 0x6E, 0xE6, 0xDD, 0xDD, 0xD9, 0x99, 0xBB, 0xBB, 0x67, 0x63,
    0x6E, 0x0E, 0xEC, 0xCC, 0xDD, 0xDC, 0x99, 0x9F, 0xBB, 0xB9, 0x33, 0x3E,
};

#define MAX_ROM_SIZE (8u * 1024u * 1024u) /* largest official size (code 0x08) */

/* ------------------------------------------------------------------------- */
/* Header                                                                     */
/* ------------------------------------------------------------------------- */

const char *cartridgeTypeName(uint8_t type) {
    switch (type) {
        case 0x00: return "ROM ONLY";
        case 0x01: return "MBC1";
        case 0x02: return "MBC1+RAM";
        case 0x03: return "MBC1+RAM+BATTERY";
        case 0x05: return "MBC2";
        case 0x06: return "MBC2+BATTERY";
        case 0x08: return "ROM+RAM";
        case 0x09: return "ROM+RAM+BATTERY";
        case 0x0B: return "MMM01";
        case 0x0C: return "MMM01+RAM";
        case 0x0D: return "MMM01+RAM+BATTERY";
        case 0x0F: return "MBC3+TIMER+BATTERY";
        case 0x10: return "MBC3+TIMER+RAM+BATTERY";
        case 0x11: return "MBC3";
        case 0x12: return "MBC3+RAM";
        case 0x13: return "MBC3+RAM+BATTERY";
        case 0x19: return "MBC5";
        case 0x1A: return "MBC5+RAM";
        case 0x1B: return "MBC5+RAM+BATTERY";
        case 0x1C: return "MBC5+RUMBLE";
        case 0x1D: return "MBC5+RUMBLE+RAM";
        case 0x1E: return "MBC5+RUMBLE+RAM+BATTERY";
        case 0x20: return "MBC6";
        case 0x22: return "MBC7+SENSOR+RUMBLE+RAM+BATTERY";
        case 0xFC: return "POCKET CAMERA";
        case 0xFD: return "BANDAI TAMA5";
        case 0xFE: return "HuC3";
        case 0xFF: return "HuC1+RAM+BATTERY";
        default:   return "UNKNOWN";
    }
}

bool cartridgeTypeSupported(uint8_t type) {
    switch (type) {
        case 0x00: case 0x08: case 0x09:  /* no MBC      */
        case 0x01: case 0x02: case 0x03:  /* MBC1        */
        case 0x0F: case 0x10: case 0x11:  /* MBC3        */
        case 0x12: case 0x13:
        case 0x19: case 0x1A: case 0x1B:  /* MBC5        */
        case 0x1C: case 0x1D: case 0x1E:
            return true;
        default:
            return false;
    }
}

static enum mbcType typeMbc(uint8_t type) {
    if (type >= 0x01 && type <= 0x03) return MBC_1;
    if (type >= 0x0F && type <= 0x13) return MBC_3;
    if (type >= 0x19 && type <= 0x1E) return MBC_5;
    return MBC_NONE;
}

static bool typeHasRam(uint8_t type) {
    switch (type) {
        case 0x02: case 0x03: case 0x08: case 0x09:
        case 0x10: case 0x12: case 0x13:
        case 0x1A: case 0x1B: case 0x1D: case 0x1E:
            return true;
        default:
            return false;
    }
}

static bool typeHasBattery(uint8_t type) {
    switch (type) {
        case 0x03: case 0x09: case 0x0F: case 0x10: case 0x13: case 0x1B: case 0x1E:
            return true;
        default:
            return false;
    }
}

static size_t ramSizeFromCode(uint8_t code) {
    switch (code) {
        case 0x01: return 2 * 1024;   /* unofficial, used by some homebrew */
        case 0x02: return 8 * 1024;
        case 0x03: return 32 * 1024;
        case 0x04: return 128 * 1024;
        case 0x05: return 64 * 1024;
        default:   return 0;
    }
}

cartError cartridgeParseHeader(const uint8_t *data, size_t size, struct cartridgeHeader *out) {
    uint8_t checksum = 0;
    int i;

    if (size < HEADER_END) return CART_ERR_TOO_SMALL;

    memset(out, 0, sizeof(*out));
    /* Title: up to 16 characters, may be shorter (padded with 0). */
    for (i = 0; i < 16; i++) {
        uint8_t ch = data[HEADER_TITLE + i];
        if (ch == 0) break;
        out->title[i] = (ch >= 0x20 && ch < 0x7F) ? (char)ch : '?';
    }
    out->title[i] = '\0';

    out->cgbFlag = data[HEADER_CGB_FLAG];
    out->sgbFlag = data[HEADER_SGB_FLAG];
    out->type = data[HEADER_TYPE];
    out->romSizeCode = data[HEADER_ROM_SIZE];
    out->ramSizeCode = data[HEADER_RAM_SIZE];
    out->destination = data[HEADER_DESTINATION];
    out->oldLicensee = data[HEADER_OLD_LICENSEE];
    out->version = data[HEADER_VERSION];
    out->headerChecksum = data[HEADER_CHECKSUM];
    out->globalChecksum = (uint16_t)((data[HEADER_GLOBAL_CHECKSUM] << 8) | data[HEADER_GLOBAL_CHECKSUM + 1]);

    /* Header checksum over 0134-014C, verified by the boot ROM. */
    for (i = HEADER_TITLE; i <= HEADER_VERSION; i++) checksum = (uint8_t)(checksum - data[i] - 1);
    out->computedHeaderChecksum = checksum;
    out->headerChecksumValid = (checksum == out->headerChecksum);
    out->logoValid = memcmp(&data[HEADER_LOGO], nintendoLogo, sizeof(nintendoLogo)) == 0;

    /* Neither the logo nor the checksum match: this is not a Game Boy ROM. */
    if (!out->logoValid && !out->headerChecksumValid) return CART_ERR_INVALID;
    /* It looks like a Game Boy ROM, but the header must have a sane ROM size. */
    if (out->romSizeCode > 0x08) return CART_ERR_BAD_HEADER;
    return CART_OK;
}

void cartridgePrintInfo(FILE *out) {
    const struct cartridgeHeader *h = &cartridge.header;
    fprintf(out, "Title:            %s\n", h->title);
    fprintf(out, "Cartridge type:   0x%02X (%s)%s\n", h->type, cartridgeTypeName(h->type),
            cartridgeTypeSupported(h->type) ? "" : " - UNSUPPORTED");
    fprintf(out, "ROM size:         %u KiB (%u banks, code 0x%02X)\n",
            (unsigned)(cartridge.romSize / 1024), cartridge.romBanks, h->romSizeCode);
    fprintf(out, "RAM size:         %u KiB (code 0x%02X)%s\n",
            (unsigned)(cartridge.ramSize / 1024), h->ramSizeCode,
            cartridge.hasBattery ? ", battery backed" : "");
    fprintf(out, "CGB flag:         0x%02X%s\n", h->cgbFlag,
            h->cgbFlag == 0xC0 ? " (CGB only - may not run on DMG)" :
            h->cgbFlag == 0x80 ? " (CGB enhanced, DMG compatible)" : "");
    fprintf(out, "SGB flag:         0x%02X\n", h->sgbFlag);
    fprintf(out, "Destination:      %s\n", h->destination ? "Overseas" : "Japan");
    fprintf(out, "Version:          %u\n", h->version);
    fprintf(out, "Nintendo logo:    %s\n", h->logoValid ? "valid" : "INVALID");
    fprintf(out, "Header checksum:  0x%02X (%s, computed 0x%02X)\n", h->headerChecksum,
            h->headerChecksumValid ? "valid" : "INVALID", h->computedHeaderChecksum);
    fprintf(out, "Global checksum:  0x%04X\n", h->globalChecksum);
}

const char *cartridgeErrorString(cartError error) {
    switch (error) {
        case CART_OK:              return "no error";
        case CART_ERR_OPEN:        return "could not open the ROM file (does it exist?)";
        case CART_ERR_READ:        return "could not read the ROM file";
        case CART_ERR_TOO_SMALL:   return "file is too small to be a Game Boy ROM (needs a 0x150 byte header)";
        case CART_ERR_TOO_LARGE:   return "file is larger than any Game Boy ROM (8 MiB)";
        case CART_ERR_INVALID:     return "not a Game Boy ROM (Nintendo logo and header checksum both invalid)";
        case CART_ERR_BAD_HEADER:  return "corrupted cartridge header (invalid ROM size code)";
        case CART_ERR_UNSUPPORTED: return "unsupported cartridge type (supported: ROM ONLY, MBC1, MBC3, MBC5)";
        case CART_ERR_NO_MEMORY:   return "out of memory";
        default:                   return "unknown error";
    }
}

/* ------------------------------------------------------------------------- */
/* Loading                                                                    */
/* ------------------------------------------------------------------------- */

void cartridgeUnload(void) {
    free(cartridge.rom);
    free(cartridge.ram);
    memset(&cartridge, 0, sizeof(cartridge));
}

void cartridgeResetMbc(void) {
    cartridge.ramEnabled = false;
    cartridge.bank1 = 1;
    cartridge.bank2 = 0;
    cartridge.mode = 0;
    cartridge.romBank = 1;
    cartridge.ramBank = 0;
    cartridge.rtc.latchWrite = 0xFF;
    if (cartridge.rtc.lastUpdate == 0) cartridge.rtc.lastUpdate = (int64_t)time(NULL);
}

cartError cartridgeLoadBuffer(const uint8_t *data, size_t size) {
    struct cartridgeHeader header;
    size_t declaredSize;
    size_t romSize;
    cartError error;

    if (size > MAX_ROM_SIZE) return CART_ERR_TOO_LARGE;
    error = cartridgeParseHeader(data, size, &header);
    if (error != CART_OK) return error;
    if (!cartridgeTypeSupported(header.type)) {
        cartridgeUnload();
        cartridge.header = header; /* keep it so the caller can report the type */
        return CART_ERR_UNSUPPORTED;
    }

    /* Size the ROM from the header; pad a short (e.g. trimmed homebrew) file
     * with 0xFF and round up to a whole number of 16 KiB banks. */
    declaredSize = (size_t)ROM_BANK_SIZE * 2u << header.romSizeCode;
    romSize = declaredSize > size ? declaredSize : size;
    romSize = (romSize + ROM_BANK_SIZE - 1) / ROM_BANK_SIZE * ROM_BANK_SIZE;

    cartridgeUnload();
    cartridge.rom = malloc(romSize);
    if (!cartridge.rom) return CART_ERR_NO_MEMORY;
    memset(cartridge.rom, 0xFF, romSize);
    memcpy(cartridge.rom, data, size);

    cartridge.header = header;
    cartridge.romSize = romSize;
    cartridge.romBanks = (unsigned)(romSize / ROM_BANK_SIZE);
    cartridge.mbc = typeMbc(header.type);
    cartridge.hasRam = typeHasRam(header.type);
    cartridge.hasBattery = typeHasBattery(header.type);
    cartridge.hasRtc = (header.type == 0x0F || header.type == 0x10);
    cartridge.hasRumble = (header.type >= 0x1C && header.type <= 0x1E);

    if (cartridge.hasRam) {
        size_t maxRam = cartridge.mbc == MBC_NONE ? RAM_BANK_SIZE
                      : cartridge.mbc == MBC_1   ? 4u * RAM_BANK_SIZE
                      : cartridge.mbc == MBC_3   ? 8u * RAM_BANK_SIZE   /* 64 KiB for MBC30 */
                      :                            16u * RAM_BANK_SIZE; /* MBC5: 128 KiB   */
        cartridge.ramSize = ramSizeFromCode(header.ramSizeCode);
        if (cartridge.ramSize == 0) cartridge.ramSize = RAM_BANK_SIZE; /* header says RAM, size missing */
        if (cartridge.ramSize > maxRam) cartridge.ramSize = maxRam;
        cartridge.ram = malloc(cartridge.ramSize);
        if (!cartridge.ram) { cartridgeUnload(); return CART_ERR_NO_MEMORY; }
        memset(cartridge.ram, 0xFF, cartridge.ramSize);
        cartridge.ramBanks = (unsigned)((cartridge.ramSize + RAM_BANK_SIZE - 1) / RAM_BANK_SIZE);
    }

    cartridgeResetMbc();
    cartridge.loaded = true;
    return CART_OK;
}

cartError cartridgeLoadFile(const char *path) {
    FILE *file;
    long length;
    uint8_t *data;
    cartError error;
    size_t pathLength;

    file = fopen(path, "rb");
    if (!file) return CART_ERR_OPEN;

    if (fseek(file, 0, SEEK_END) != 0 || (length = ftell(file)) < 0 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return CART_ERR_READ;
    }
    if ((size_t)length < HEADER_END) { fclose(file); return CART_ERR_TOO_SMALL; }
    if ((size_t)length > MAX_ROM_SIZE) { fclose(file); return CART_ERR_TOO_LARGE; }

    data = malloc((size_t)length);
    if (!data) { fclose(file); return CART_ERR_NO_MEMORY; }
    if (fread(data, 1, (size_t)length, file) != (size_t)length) {
        free(data);
        fclose(file);
        return CART_ERR_READ;
    }
    fclose(file);

    error = cartridgeLoadBuffer(data, (size_t)length);
    free(data);
    if (error != CART_OK) return error;

    /* Save file: replace the extension with .sav */
    pathLength = strlen(path);
    if (pathLength + 5 < sizeof(cartridge.savePath)) {
        char *dot;
        char *slash;
        strcpy(cartridge.savePath, path);
        dot = strrchr(cartridge.savePath, '.');
        slash = strrchr(cartridge.savePath, '/');
        if (!slash) slash = strrchr(cartridge.savePath, '\\');
        if (dot && (!slash || dot > slash)) *dot = '\0';
        strcat(cartridge.savePath, ".sav");
    }
    if (cartridge.hasBattery) cartridgeLoadRam();
    return CART_OK;
}

/* ------------------------------------------------------------------------- */
/* Battery-backed save files                                                  */
/*                                                                            */
/* <rom>.sav holds the cartridge RAM. For MBC3 carts with a clock, 48 bytes   */
/* follow it in the format BGB and VBA-M use: 5 live RTC registers and 5      */
/* latched ones as 32-bit little-endian values, then a 64-bit Unix time.      */
/* ------------------------------------------------------------------------- */

#define RTC_SAVE_SIZE 48

static void put32le(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

static uint32_t get32le(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

bool cartridgeLoadRam(void) {
    FILE *file;
    size_t read = 0;
    uint8_t footer[RTC_SAVE_SIZE];

    if (!cartridge.hasBattery || !cartridge.savePath[0]) return false;
    file = fopen(cartridge.savePath, "rb");
    if (!file) return false;
    if (cartridge.ram) read = fread(cartridge.ram, 1, cartridge.ramSize, file);
    if (cartridge.hasRtc && fread(footer, 1, RTC_SAVE_SIZE, file) >= 44) {
        struct rtc *r = &cartridge.rtc;
        int i;
        r->seconds = (uint8_t)get32le(footer + 0);
        r->minutes = (uint8_t)get32le(footer + 4);
        r->hours = (uint8_t)get32le(footer + 8);
        r->daysLow = (uint8_t)get32le(footer + 12);
        r->daysHigh = (uint8_t)get32le(footer + 16);
        for (i = 0; i < 5; i++) r->latched[i] = (uint8_t)get32le(footer + 20 + i * 4);
        r->lastUpdate = (int64_t)get32le(footer + 40) | ((int64_t)get32le(footer + 44) << 32);
        cartridgeRtcUpdate((int64_t)time(NULL)); /* the clock kept running while we were off */
        read++;
    }
    fclose(file);
    return read > 0;
}

bool cartridgeSaveRam(void) {
    FILE *file;
    bool ok = true;

    if (!cartridge.hasBattery || (!cartridge.ram && !cartridge.hasRtc) || !cartridge.savePath[0]) return false;
    file = fopen(cartridge.savePath, "wb");
    if (!file) return false;
    if (cartridge.ram) ok = fwrite(cartridge.ram, 1, cartridge.ramSize, file) == cartridge.ramSize;
    if (cartridge.hasRtc) {
        struct rtc *r = &cartridge.rtc;
        uint8_t footer[RTC_SAVE_SIZE];
        int i;
        cartridgeRtcUpdate((int64_t)time(NULL));
        put32le(footer + 0, r->seconds);
        put32le(footer + 4, r->minutes);
        put32le(footer + 8, r->hours);
        put32le(footer + 12, r->daysLow);
        put32le(footer + 16, r->daysHigh);
        for (i = 0; i < 5; i++) put32le(footer + 20 + i * 4, r->latched[i]);
        put32le(footer + 40, (uint32_t)r->lastUpdate);
        put32le(footer + 44, (uint32_t)((uint64_t)r->lastUpdate >> 32));
        ok = fwrite(footer, 1, RTC_SAVE_SIZE, file) == RTC_SAVE_SIZE && ok;
    }
    ok = (fclose(file) == 0) && ok;
    return ok;
}

/* ------------------------------------------------------------------------- */
/* MBC1                                                                       */
/*                                                                            */
/*   0000-1FFF  write: RAM enable (0x0A in low nibble enables)                */
/*   2000-3FFF  write: BANK1, 5-bit ROM bank number (0 is treated as 1)       */
/*   4000-5FFF  write: BANK2, 2 bits: ROM bank bits 5-6, or RAM bank          */
/*   6000-7FFF  write: MODE, 0 = simple, 1 = BANK2 also affects 0000-3FFF     */
/*                     and selects the RAM bank                               */
/* ------------------------------------------------------------------------- */

static unsigned mbc1RomBankLow(void) {
    /* 0000-3FFF: bank 0, or BANK2<<5 in mode 1 (large ROMs) */
    return cartridge.mode ? (unsigned)(cartridge.bank2 << 5) : 0u;
}

static unsigned mbc1RomBankHigh(void) {
    return (unsigned)((cartridge.bank2 << 5) | cartridge.bank1);
}

static unsigned mbc1RamBank(void) {
    return cartridge.mode ? cartridge.bank2 : 0u;
}

static void mbc1Write(uint16_t address, uint8_t value) {
    if (address < 0x2000) {
        cartridge.ramEnabled = (value & 0x0F) == 0x0A;
    } else if (address < 0x4000) {
        cartridge.bank1 = value & 0x1F;
        if (cartridge.bank1 == 0) cartridge.bank1 = 1; /* checked on the 5 bits, before masking */
    } else if (address < 0x6000) {
        cartridge.bank2 = value & 0x03;
    } else {
        cartridge.mode = value & 0x01;
    }
}

/* ------------------------------------------------------------------------- */
/* MBC3                                                                       */
/*                                                                            */
/*   0000-1FFF  RAM and clock enable (0x0A)                                   */
/*   2000-3FFF  7-bit ROM bank (0 is treated as 1)                            */
/*   4000-5FFF  00-03: RAM bank, 08-0C: map a clock register at A000          */
/*   6000-7FFF  write 00 then 01: latch the clock into the readable copy      */
/*                                                                            */
/* Clock registers: 08 seconds, 09 minutes, 0A hours, 0B day bits 0-7,        */
/* 0C bit 0 = day bit 8, bit 6 = halt, bit 7 = day counter overflow.          */
/* ------------------------------------------------------------------------- */

static const uint8_t rtcMask[5] = { 0x3F, 0x3F, 0x1F, 0xFF, 0xC1 };

void cartridgeRtcUpdate(int64_t now) {
    struct rtc *r = &cartridge.rtc;
    int64_t elapsed = now - r->lastUpdate;
    int64_t total;
    int64_t days;

    r->lastUpdate = now;
    if (elapsed <= 0 || (r->daysHigh & 0x40)) return; /* halted, or clock went back */

    days = r->daysLow | ((r->daysHigh & 0x01) << 8);
    total = r->seconds + r->minutes * 60 + r->hours * 3600 + days * 86400 + elapsed;

    r->seconds = (uint8_t)(total % 60);
    r->minutes = (uint8_t)(total / 60 % 60);
    r->hours = (uint8_t)(total / 3600 % 24);
    days = total / 86400;
    if (days > 511) {
        days %= 512;
        r->daysHigh |= 0x80; /* overflow flag stays set until the game clears it */
    }
    r->daysLow = (uint8_t)(days & 0xFF);
    r->daysHigh = (uint8_t)((r->daysHigh & 0xC0) | ((days >> 8) & 0x01));
}

static uint8_t *rtcRegister(uint8_t index) {
    struct rtc *r = &cartridge.rtc;
    switch (index) {
        case 0: return &r->seconds;
        case 1: return &r->minutes;
        case 2: return &r->hours;
        case 3: return &r->daysLow;
        default: return &r->daysHigh;
    }
}

static void mbc3Write(uint16_t address, uint8_t value) {
    if (address < 0x2000) {
        cartridge.ramEnabled = (value & 0x0F) == 0x0A;
    } else if (address < 0x4000) {
        /* MBC30 (more than 128 banks) uses all 8 bits */
        cartridge.romBank = (uint16_t)(cartridge.romBanks > 128 ? value : (value & 0x7F));
        if (cartridge.romBank == 0) cartridge.romBank = 1;
    } else if (address < 0x6000) {
        cartridge.ramBank = value;
    } else {
        if (cartridge.hasRtc && cartridge.rtc.latchWrite == 0x00 && value == 0x01) {
            int i;
            cartridgeRtcUpdate((int64_t)time(NULL));
            for (i = 0; i < 5; i++) cartridge.rtc.latched[i] = *rtcRegister((uint8_t)i);
        }
        cartridge.rtc.latchWrite = value;
    }
}

/* ------------------------------------------------------------------------- */
/* MBC5                                                                       */
/*                                                                            */
/*   0000-1FFF  RAM enable (0x0A)                                             */
/*   2000-2FFF  ROM bank bits 0-7 (bank 0 CAN be selected at 4000)            */
/*   3000-3FFF  ROM bank bit 8                                                */
/*   4000-5FFF  RAM bank 0-15 (bit 3 drives the motor on rumble carts)        */
/* ------------------------------------------------------------------------- */

static void mbc5Write(uint16_t address, uint8_t value) {
    if (address < 0x2000) {
        cartridge.ramEnabled = (value & 0x0F) == 0x0A;
    } else if (address < 0x3000) {
        cartridge.romBank = (uint16_t)((cartridge.romBank & 0x100) | value);
    } else if (address < 0x4000) {
        cartridge.romBank = (uint16_t)((cartridge.romBank & 0xFF) | ((value & 0x01) << 8));
    } else if (address < 0x6000) {
        cartridge.ramBank = (uint8_t)(value & (cartridge.hasRumble ? 0x07 : 0x0F));
    }
}

/* ------------------------------------------------------------------------- */
/* Bus interface                                                              */
/* ------------------------------------------------------------------------- */

/* Offset into cartridge RAM for an A000-BFFF access, or -1 if unmapped. */
static long ramOffset(uint16_t address) {
    unsigned bank = 0;

    if (!cartridge.ram) return -1;
    switch (cartridge.mbc) {
        case MBC_NONE: bank = 0; break;
        case MBC_1:    if (!cartridge.ramEnabled) return -1; bank = mbc1RamBank(); break;
        case MBC_3:    if (!cartridge.ramEnabled || cartridge.ramBank > 0x07) return -1; bank = cartridge.ramBank; break;
        case MBC_5:    if (!cartridge.ramEnabled) return -1; bank = cartridge.ramBank; break;
    }
    bank %= cartridge.ramBanks;
    {
        size_t offset = (size_t)bank * RAM_BANK_SIZE + (address - 0xA000u);
        return offset < cartridge.ramSize ? (long)offset : -1;
    }
}

static bool rtcMapped(void) {
    return cartridge.mbc == MBC_3 && cartridge.hasRtc && cartridge.ramEnabled &&
           cartridge.ramBank >= 0x08 && cartridge.ramBank <= 0x0C;
}

uint8_t cartridgeRead(uint16_t address) {
    if (!cartridge.loaded) return 0xFF;

    if (address < 0x4000) {
        unsigned bank = cartridge.mbc == MBC_1 ? mbc1RomBankLow() : 0u;
        bank %= cartridge.romBanks; /* unused high bits are not connected */
        return cartridge.rom[(size_t)bank * ROM_BANK_SIZE + address];
    }

    if (address < 0x8000) {
        unsigned bank;
        switch (cartridge.mbc) {
            case MBC_1: bank = mbc1RomBankHigh(); break;
            case MBC_3:
            case MBC_5: bank = cartridge.romBank; break;
            default:    bank = 1; break;
        }
        bank %= cartridge.romBanks;
        return cartridge.rom[(size_t)bank * ROM_BANK_SIZE + (address - 0x4000)];
    }

    if (address >= 0xA000 && address < 0xC000) {
        long offset;
        if (rtcMapped()) {
            uint8_t index = (uint8_t)(cartridge.ramBank - 0x08);
            return (uint8_t)(cartridge.rtc.latched[index] & rtcMask[index]);
        }
        offset = ramOffset(address);
        return offset >= 0 ? cartridge.ram[offset] : 0xFF;
    }

    return 0xFF;
}

void cartridgeWrite(uint16_t address, uint8_t value) {
    if (!cartridge.loaded) return;

    if (address < 0x8000) {
        switch (cartridge.mbc) {
            case MBC_1: mbc1Write(address, value); break;
            case MBC_3: mbc3Write(address, value); break;
            case MBC_5: mbc5Write(address, value); break;
            default: break; /* ROM only: writes are ignored */
        }
        return;
    }

    if (address >= 0xA000 && address < 0xC000) {
        long offset;
        if (rtcMapped()) {
            uint8_t index = (uint8_t)(cartridge.ramBank - 0x08);
            cartridgeRtcUpdate((int64_t)time(NULL));
            *rtcRegister(index) = (uint8_t)(value & rtcMask[index]);
            cartridge.rtc.latched[index] = (uint8_t)(value & rtcMask[index]);
            return;
        }
        offset = ramOffset(address);
        if (offset >= 0) cartridge.ram[offset] = value;
    }
}
