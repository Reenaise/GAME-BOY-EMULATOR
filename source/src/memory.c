/*
 * Memory bus (Cinoop: memory.c).
 *
 * readByte()/writeByte() decode the address and forward the access to the
 * component that owns it. Nothing else in the emulator touches another
 * component's memory directly - the CPU, the DMA copy and the debugger all go
 * through here.
 */

#include "memory.h"

#include <string.h>

#include "cartridge.h"
#include "debugger.h"
#include "input.h"
#include "interrupts.h"
#include "ppu.h"
#include "registers.h"
#include "serial.h"
#include "timer.h"

uint8_t vram[VRAM_SIZE];
uint8_t wram[WRAM_SIZE];
uint8_t oam[OAM_SIZE];
uint8_t hram[HRAM_SIZE];
uint8_t io[IO_SIZE];

static const uint8_t *bootRom;
static bool bootRomMapped;

/*
 * Sound registers FF10-FF3F.
 *
 * SOUND IS INTENTIONALLY NOT IMPLEMENTED. Games still write to these
 * registers, so they are stored and read back with the bits that always
 * read as 1 on hardware (Pan Docs), which is what most software expects.
 * No channel is ever active, so NR52 reports all channels off.
 */
static const uint8_t soundReadMask[0x30] = {
    /* FF10 */ 0x80, 0x3F, 0x00, 0xFF, 0xBF, /* NR10-NR14 */
    /* FF15 */ 0xFF, 0x3F, 0x00, 0xFF, 0xBF, /* NR21-NR24 */
    /* FF1A */ 0x7F, 0xFF, 0x9F, 0xFF, 0xBF, /* NR30-NR34 */
    /* FF1F */ 0xFF, 0xFF, 0x00, 0x00, 0xBF, /* NR41-NR44 */
    /* FF24 */ 0x00, 0x00, 0x70,             /* NR50 NR51 NR52 */
    /* FF27 */ 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    /* FF30 wave RAM */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};

static uint8_t soundRead(uint16_t address) {
    unsigned index = address - 0xFF10u;
    if (address == 0xFF26) return (uint8_t)((io[0x26] & 0x80) | 0x70); /* power bit only */
    return (uint8_t)(io[address - 0xFF00] | soundReadMask[index]);
}

static void soundWrite(uint16_t address, uint8_t value) {
    io[address - 0xFF00] = value;
}

void memoryReset(void) {
    memset(vram, 0, sizeof(vram));
    memset(wram, 0, sizeof(wram));
    memset(oam, 0, sizeof(oam));
    memset(hram, 0, sizeof(hram));
    memset(io, 0, sizeof(io));

    /* Sound register values left by the boot ROM (stored, never played). */
    io[0x10] = 0x80; io[0x11] = 0xBF; io[0x12] = 0xF3; io[0x14] = 0xBF;
    io[0x16] = 0x3F; io[0x19] = 0xBF; io[0x1A] = 0x7F; io[0x1B] = 0xFF;
    io[0x1C] = 0x9F; io[0x1E] = 0xBF; io[0x20] = 0xFF; io[0x23] = 0xBF;
    io[0x24] = 0x77; io[0x25] = 0xF3; io[0x26] = 0xF1;

    bootRomMapped = (bootRom != NULL);
}

void memorySetBootRom(const uint8_t *data) {
    bootRom = data;
    bootRomMapped = (data != NULL);
}

bool memoryBootRomMapped(void) {
    return bootRomMapped;
}

/* OAM DMA: copy 160 bytes from XX00-XX9F to OAM.
 * Simplification: done instantly instead of over 160 M-cycles. */
static void dmaTransfer(uint8_t sourceHigh) {
    uint16_t source = (uint16_t)(sourceHigh << 8);
    int i;
    if (source >= 0xE000) source -= 0x2000; /* E0-FF read from WRAM */
    for (i = 0; i < OAM_SIZE; i++) oam[i] = readByte((uint16_t)(source + i));
}

static uint8_t ioRead(uint16_t address) {
    if (address == 0xFF00) return inputRead();
    if (address == 0xFF01 || address == 0xFF02) return serialRead(address);
    if (address >= 0xFF04 && address <= 0xFF07) return timerRead(address);
    if (address == 0xFF0F) return (uint8_t)(interrupt.flags | 0xE0);
    if (address >= 0xFF10 && address <= 0xFF3F) return soundRead(address);
    if (address >= 0xFF40 && address <= 0xFF4B) return ppuRead(address);
    /* Unmapped / CGB-only registers read as 0xFF on the DMG. */
    return 0xFF;
}

static void ioWrite(uint16_t address, uint8_t value) {
    if (address == 0xFF00) { inputWrite(value); return; }
    if (address == 0xFF01 || address == 0xFF02) { serialWrite(address, value); return; }
    if (address >= 0xFF04 && address <= 0xFF07) { timerWrite(address, value); return; }
    if (address == 0xFF0F) { interrupt.flags = value & INTERRUPT_MASK; return; }
    if (address >= 0xFF10 && address <= 0xFF3F) { soundWrite(address, value); return; }
    if (address == 0xFF46) { ppuWrite(address, value); dmaTransfer(value); return; }
    if (address >= 0xFF40 && address <= 0xFF4B) { ppuWrite(address, value); return; }
    if (address == 0xFF50) { if (value) bootRomMapped = false; return; }
    io[address - 0xFF00] = value;
}

uint8_t readByte(uint16_t address) {
    if (address < 0x8000) {
        if (bootRomMapped && address < 0x0100) return bootRom[address];
        return cartridgeRead(address);
    }
    if (address < 0xA000) return vram[address - 0x8000];
    if (address < 0xC000) return cartridgeRead(address);
    if (address < 0xE000) return wram[address - 0xC000];
    if (address < 0xFE00) return wram[address - 0xE000]; /* echo RAM */
    if (address < 0xFEA0) return oam[address - 0xFE00];
    if (address < 0xFF00) return 0xFF;                   /* unusable */
    if (address < 0xFF80) return ioRead(address);
    if (address < 0xFFFF) return hram[address - 0xFF80];
    return interrupt.enable;
}

void writeByte(uint16_t address, uint8_t value) {
    if (debugger.watchpointCount) debuggerOnWrite(address, value);

    if (address < 0x8000) cartridgeWrite(address, value);       /* MBC registers */
    else if (address < 0xA000) vram[address - 0x8000] = value;
    else if (address < 0xC000) cartridgeWrite(address, value);  /* cartridge RAM */
    else if (address < 0xE000) wram[address - 0xC000] = value;
    else if (address < 0xFE00) wram[address - 0xE000] = value;
    else if (address < 0xFEA0) oam[address - 0xFE00] = value;
    else if (address < 0xFF00) { /* unusable: ignored */ }
    else if (address < 0xFF80) ioWrite(address, value);
    else if (address < 0xFFFF) hram[address - 0xFF80] = value;
    else interrupt.enable = value;
}

uint16_t readShort(uint16_t address) {
    return (uint16_t)(readByte(address) | (readByte((uint16_t)(address + 1)) << 8));
}

void writeShort(uint16_t address, uint16_t value) {
    writeByte(address, (uint8_t)(value & 0xFF));
    writeByte((uint16_t)(address + 1), (uint8_t)(value >> 8));
}

void writeShortToStack(uint16_t value) {
    registers.sp -= 2;
    writeShort(registers.sp, value);
}

uint16_t readShortFromStack(void) {
    uint16_t value = readShort(registers.sp);
    registers.sp += 2;
    return value;
}
