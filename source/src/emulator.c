/*
 * Ties the components together. This is the loop from Cinoop's main.c:
 *
 *     while (running) {
 *         cpuStep();           // execute one instruction, get its cycles
 *         gpuStep();           // advance the PPU by the same cycles
 *         interruptStep();     // (done inside cpuStep here)
 *     }
 *
 * plus the timer and serial port, which Cinoop does not have. Every
 * component advances by exactly the number of cycles the CPU used, so they
 * stay in lock-step.
 */

#include "emulator.h"

#include <stdio.h>
#include <string.h>

#include "cpu.h"
#include "debugger.h"
#include "input.h"
#include "interrupts.h"
#include "memory.h"
#include "ppu.h"
#include "serial.h"
#include "timer.h"

static uint8_t bootRom[256];
static bool haveBootRom;

static bool loadBootRom(const char *path) {
    FILE *file = fopen(path, "rb");
    size_t read;
    if (!file) return false;
    read = fread(bootRom, 1, sizeof(bootRom), file);
    fclose(file);
    return read == sizeof(bootRom);
}

cartError emulatorLoad(const char *romPath, const char *bootRomPath) {
    cartError error = cartridgeLoadFile(romPath);
    if (error != CART_OK) return error;

    haveBootRom = false;
    if (bootRomPath) {
        if (loadBootRom(bootRomPath)) {
            haveBootRom = true;
        } else {
            fprintf(stderr, "Warning: could not read a 256 byte boot ROM from '%s', "
                            "starting without it.\n", bootRomPath);
        }
    }
    memorySetBootRom(haveBootRom ? bootRom : NULL);

    emulatorReset();
    return CART_OK;
}

void emulatorReset(void) {
    memoryReset();
    interruptsReset();
    timerReset();
    serialReset();
    ppuReset();
    inputReset();
    cartridgeResetMbc();

    if (memoryBootRomMapped()) {
        /* Power-on state: the boot ROM initialises everything itself. */
        cpuResetForBootRom();
        ppu.lcdc = 0;
        ppu.bgp = 0;
        timer.divCounter = 0;
        interrupt.flags = 0;
    } else {
        cpuReset();
    }
}

void emulatorShutdown(void) {
    cartridgeSaveRam();
    cartridgeUnload();
}

int emulatorStep(void) {
    int cycles;

    if (debugger.trace) debuggerTraceInstruction();

    cycles = cpuStep();
    timerStep(cycles);
    serialStep(cycles);
    ppuStep(cycles);
    return cycles;
}

runResult emulatorRunFrame(void) {
    int elapsed = 0;

    ppu.frameReady = false;
    while (!ppu.frameReady) {
        if (debugger.enabled && debuggerShouldBreak()) return RUN_BREAKPOINT;

        elapsed += emulatorStep();

        /* With the LCD off no V-Blank happens; still return about once per
         * frame so the window stays responsive. */
        if (!(ppu.lcdc & LCDC_LCD_ENABLE) && elapsed >= PPU_FRAME_CYCLES) break;
        if (elapsed >= 2 * PPU_FRAME_CYCLES) break;
    }
    return RUN_FRAME_COMPLETE;
}
