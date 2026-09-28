/*
 * PNG screenshots without zlib: the image data is stored in uncompressed
 * ("stored") deflate blocks, which every PNG reader accepts.
 */

#include "screenshot.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Classic DMG green-ish shades, lightest to darkest. */
uint32_t displayPalette[4] = { 0xE0F8D0, 0x88C070, 0x346856, 0x081820 };

static uint32_t crcTable[256];

static void buildCrcTable(void) {
    uint32_t n, k;
    for (n = 0; n < 256; n++) {
        uint32_t c = n;
        for (k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        crcTable[n] = c;
    }
}

static uint32_t crc32Update(uint32_t crc, const uint8_t *data, size_t length) {
    size_t i;
    for (i = 0; i < length; i++) crc = crcTable[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    return crc;
}

static void put32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v;
}

static bool writeChunk(FILE *file, const char *type, const uint8_t *data, uint32_t length) {
    uint8_t header[8];
    uint8_t footer[4];
    uint32_t crc;

    put32(header, length);
    memcpy(header + 4, type, 4);
    crc = crc32Update(0xFFFFFFFFu, (const uint8_t *)type, 4);
    if (length) crc = crc32Update(crc, data, length);
    put32(footer, crc ^ 0xFFFFFFFFu);

    return fwrite(header, 1, 8, file) == 8 &&
           (length == 0 || fwrite(data, 1, length, file) == length) &&
           fwrite(footer, 1, 4, file) == 4;
}

bool screenshotSavePng(const char *path, const uint8_t *framebuffer) {
    static const uint8_t signature[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };
    const size_t rowSize = 1 + SCREEN_WIDTH * 3;
    const size_t rawSize = rowSize * SCREEN_HEIGHT;
    uint8_t ihdr[13];
    uint8_t *raw, *zlib, *out;
    size_t blocks, zlibSize, offset;
    uint32_t a = 1, b = 0;
    FILE *file;
    bool ok;
    int x, y;
    size_t i;

    buildCrcTable();

    raw = malloc(rawSize);
    if (!raw) return false;
    for (y = 0; y < SCREEN_HEIGHT; y++) {
        uint8_t *row = raw + (size_t)y * rowSize;
        row[0] = 0; /* filter: none */
        for (x = 0; x < SCREEN_WIDTH; x++) {
            uint32_t colour = displayPalette[framebuffer[y * SCREEN_WIDTH + x] & 3];
            row[1 + x * 3] = (uint8_t)(colour >> 16);
            row[2 + x * 3] = (uint8_t)(colour >> 8);
            row[3 + x * 3] = (uint8_t)colour;
        }
    }

    /* zlib header + stored blocks (max 65535 bytes each) + Adler-32 */
    blocks = (rawSize + 65534) / 65535;
    zlibSize = 2 + blocks * 5 + rawSize + 4;
    zlib = malloc(zlibSize);
    if (!zlib) { free(raw); return false; }
    out = zlib;
    *out++ = 0x78;
    *out++ = 0x01;
    for (offset = 0; offset < rawSize; offset += 65535) {
        size_t length = rawSize - offset > 65535 ? 65535 : rawSize - offset;
        *out++ = (uint8_t)(offset + length >= rawSize ? 1 : 0);
        *out++ = (uint8_t)length;
        *out++ = (uint8_t)(length >> 8);
        *out++ = (uint8_t)~length;
        *out++ = (uint8_t)(~length >> 8);
        memcpy(out, raw + offset, length);
        out += length;
    }
    for (i = 0; i < rawSize; i++) {
        a = (a + raw[i]) % 65521;
        b = (b + a) % 65521;
    }
    put32(out, (b << 16) | a);

    put32(ihdr, SCREEN_WIDTH);
    put32(ihdr + 4, SCREEN_HEIGHT);
    ihdr[8] = 8;   /* bit depth */
    ihdr[9] = 2;   /* RGB */
    ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;

    file = fopen(path, "wb");
    ok = file != NULL;
    if (ok) {
        ok = fwrite(signature, 1, 8, file) == 8 &&
             writeChunk(file, "IHDR", ihdr, 13) &&
             writeChunk(file, "IDAT", zlib, (uint32_t)zlibSize) &&
             writeChunk(file, "IEND", NULL, 0);
        ok = (fclose(file) == 0) && ok;
    }

    free(raw);
    free(zlib);
    return ok;
}
