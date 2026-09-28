#include <stdlib.h>
#include <string.h>

#include "cartridge.h"
#include "cpu.h"
#include "emulator.h"
#include "interrupts.h"
#include "memory.h"
#include "test.h"

static const uint8_t logo[48] = {
    0xCE, 0xED, 0x66, 0x66, 0xCC, 0x0D, 0x00, 0x0B, 0x03, 0x73, 0x00, 0x83,
    0x00, 0x0C, 0x00, 0x0D, 0x00, 0x08, 0x11, 0x1F, 0x88, 0x89, 0x00, 0x0E,
    0xDC, 0xCC, 0x6E, 0xE6, 0xDD, 0xDD, 0xD9, 0x99, 0xBB, 0xBB, 0x67, 0x63,
    0x6E, 0x0E, 0xEC, 0xCC, 0xDD, 0xDC, 0x99, 0x9F, 0xBB, 0xB9, 0x33, 0x3E,
};

void testBuildRom(uint8_t *rom, size_t size, uint8_t type, uint8_t romSizeCode, uint8_t ramSizeCode) {
    uint8_t checksum = 0;
    int i;

    memset(rom, 0, size);
    rom[0x100] = 0x00;             /* NOP        */
    rom[0x101] = 0xC3;             /* JP 0x0150  */
    rom[0x102] = 0x50;
    rom[0x103] = 0x01;
    memcpy(rom + HEADER_LOGO, logo, sizeof(logo));
    memcpy(rom + HEADER_TITLE, "TESTROM", 7);
    rom[HEADER_TYPE] = type;
    rom[HEADER_ROM_SIZE] = romSizeCode;
    rom[HEADER_RAM_SIZE] = ramSizeCode;
    for (i = HEADER_TITLE; i <= HEADER_VERSION; i++) checksum = (uint8_t)(checksum - rom[i] - 1);
    rom[HEADER_CHECKSUM] = checksum;
    rom[0x150] = 0x18;             /* JR -2 (infinite loop) */
    rom[0x151] = 0xFE;
}

void testResetMachine(void) {
    static uint8_t rom[0x8000];
    testBuildRom(rom, sizeof(rom), 0x00, 0x00, 0x00);
    if (cartridgeLoadBuffer(rom, sizeof(rom)) != CART_OK) {
        printf("FATAL: could not load the blank test ROM\n");
        exit(1);
    }
    memorySetBootRom(NULL);
    emulatorReset();
}

void testLoadCode(const uint8_t *code, size_t length) {
    testResetMachine();
    memcpy(wram, code, length);
    registers.pc = 0xC000;
    registers.sp = 0xDFF0;
    interrupt.master = false;
    interrupt.enable = 0;
    interrupt.flags = 0;
}

int testStep(int count) {
    int cycles = 0;
    while (count-- > 0) cycles += cpuStep();
    return cycles;
}
