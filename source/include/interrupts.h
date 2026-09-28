#ifndef GB_INTERRUPTS_H
#define GB_INTERRUPTS_H

#include <stdbool.h>
#include <stdint.h>

/* Bits in IF (FF0F) and IE (FFFF), in priority order (bit 0 highest). */
#define INTERRUPT_VBLANK  0x01
#define INTERRUPT_LCDSTAT 0x02
#define INTERRUPT_TIMER   0x04
#define INTERRUPT_SERIAL  0x08
#define INTERRUPT_JOYPAD  0x10
#define INTERRUPT_MASK    0x1F

struct interrupt {
    bool master;    /* IME: interrupt master enable (not memory mapped) */
    uint8_t enable; /* IE  (FFFF) */
    uint8_t flags;  /* IF  (FF0F), low 5 bits */
};

extern struct interrupt interrupt;

void interruptsReset(void);
void requestInterrupt(uint8_t mask);
/* True if any enabled interrupt is requested (IE & IF), regardless of IME. */
bool interruptPending(void);
/*
 * If IME is set and an interrupt is pending, service the highest-priority
 * one: clear IME and its IF bit, push PC, jump to the vector.
 * Returns the cycles used (20) or 0 if nothing was serviced.
 */
int interruptStep(void);

uint16_t interruptVector(uint8_t mask);

#endif
