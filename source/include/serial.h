#ifndef GB_SERIAL_H
#define GB_SERIAL_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

/*
 * Minimal serial port (SB FF01 / SC FF02) with no link partner attached.
 * A transfer started with the internal clock completes after 8 bits
 * (4096 T-cycles), shifts in 0xFF and raises the serial interrupt.
 * Transmitted bytes can be echoed to a FILE (test ROMs print this way).
 */
struct serial {
    uint8_t sb;
    uint8_t sc;
    bool transferring;
    int counter;
    FILE *output;
};

extern struct serial serial;

void serialReset(void);
void serialStep(int cycles);
uint8_t serialRead(uint16_t address);
void serialWrite(uint16_t address, uint8_t value);
void serialSetOutput(FILE *out);

#endif
