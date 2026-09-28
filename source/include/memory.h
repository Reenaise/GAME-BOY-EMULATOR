#ifndef GB_MEMORY_H
#define GB_MEMORY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * The memory bus. Every component (CPU, PPU, timer, cartridge, joypad) is
 * reached through readByte()/writeByte(), exactly like Cinoop's memory.c.
 *
 *   0000-3FFF  ROM bank 0            (cartridge)
 *   4000-7FFF  switchable ROM bank   (cartridge / MBC)
 *   8000-9FFF  VRAM                  (vram[])
 *   A000-BFFF  external cartridge RAM
 *   C000-DFFF  work RAM              (wram[])
 *   E000-FDFF  echo of C000-DDFF
 *   FE00-FE9F  OAM                   (oam[])
 *   FEA0-FEFF  unusable              (reads 0xFF)
 *   FF00-FF7F  I/O registers
 *   FF80-FFFE  high RAM              (hram[])
 *   FFFF       interrupt enable (IE)
 */

#define VRAM_SIZE 0x2000
#define WRAM_SIZE 0x2000
#define OAM_SIZE  0xA0
#define HRAM_SIZE 0x7F
#define IO_SIZE   0x80

extern uint8_t vram[VRAM_SIZE];
extern uint8_t wram[WRAM_SIZE];
extern uint8_t oam[OAM_SIZE];
extern uint8_t hram[HRAM_SIZE];
/* Backing store for I/O registers that no component owns (sound, unused). */
extern uint8_t io[IO_SIZE];

void memoryReset(void);

uint8_t readByte(uint16_t address);
void writeByte(uint16_t address, uint8_t value);
uint16_t readShort(uint16_t address);
void writeShort(uint16_t address, uint16_t value);

/* Stack helpers (Cinoop naming). */
void writeShortToStack(uint16_t value);
uint16_t readShortFromStack(void);

/* Optional 256-byte DMG boot ROM, mapped at 0000-00FF until FF50 is written. */
void memorySetBootRom(const uint8_t *data);
bool memoryBootRomMapped(void);

#endif
