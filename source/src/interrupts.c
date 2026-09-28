/*
 * Interrupt controller: IE (FFFF), IF (FF0F) and the IME flag.
 *
 * Cinoop's interrupts.c checks each interrupt in priority order and calls a
 * handler that pushes PC and jumps to the vector. We do the same, and take
 * the 20 T-cycles the dispatch costs on real hardware.
 */

#include "interrupts.h"

#include "memory.h"
#include "registers.h"

struct interrupt interrupt;

void interruptsReset(void) {
    interrupt.master = false;
    interrupt.enable = 0x00;
    interrupt.flags = 0x01; /* the boot ROM leaves a V-Blank request pending */
}

void requestInterrupt(uint8_t mask) {
    interrupt.flags |= (uint8_t)(mask & INTERRUPT_MASK);
}

bool interruptPending(void) {
    return (interrupt.enable & interrupt.flags & INTERRUPT_MASK) != 0;
}

uint16_t interruptVector(uint8_t mask) {
    switch (mask) {
        case INTERRUPT_VBLANK:  return 0x0040;
        case INTERRUPT_LCDSTAT: return 0x0048;
        case INTERRUPT_TIMER:   return 0x0050;
        case INTERRUPT_SERIAL:  return 0x0058;
        case INTERRUPT_JOYPAD:  return 0x0060;
        default:                return 0x0000;
    }
}

int interruptStep(void) {
    uint8_t pending;
    uint8_t mask;

    if (!interrupt.master) return 0;

    pending = (uint8_t)(interrupt.enable & interrupt.flags & INTERRUPT_MASK);
    if (!pending) return 0;

    /* Lowest set bit = highest priority (V-Blank first, Joypad last). */
    for (mask = INTERRUPT_VBLANK; mask <= INTERRUPT_JOYPAD; mask <<= 1) {
        if (pending & mask) break;
    }

    interrupt.master = false;
    interrupt.flags &= (uint8_t)~mask;
    writeShortToStack(registers.pc);
    registers.pc = interruptVector(mask);
    return 20;
}
