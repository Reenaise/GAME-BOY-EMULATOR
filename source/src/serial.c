/*
 * Serial port with nothing plugged in. Enough for games that probe the link
 * cable and for test ROMs that print their results through SB/SC.
 */

#include "serial.h"

#include "interrupts.h"

struct serial serial;

#define SERIAL_TRANSFER_CYCLES (8 * 512) /* 8 bits at 8192 Hz */

void serialReset(void) {
    FILE *out = serial.output;
    serial.sb = 0x00;
    serial.sc = 0x7E;
    serial.transferring = false;
    serial.counter = 0;
    serial.output = out; /* keep the configured output across resets */
}

void serialSetOutput(FILE *out) {
    serial.output = out;
}

void serialStep(int cycles) {
    if (!serial.transferring) return;
    serial.counter -= cycles;
    if (serial.counter <= 0) {
        serial.transferring = false;
        serial.sb = 0xFF;              /* no partner: all 1 bits shifted in */
        serial.sc &= 0x7F;             /* transfer complete                 */
        requestInterrupt(INTERRUPT_SERIAL);
    }
}

uint8_t serialRead(uint16_t address) {
    if (address == 0xFF01) return serial.sb;
    return (uint8_t)(serial.sc | 0x7E);
}

void serialWrite(uint16_t address, uint8_t value) {
    if (address == 0xFF01) {
        serial.sb = value;
        return;
    }

    serial.sc = (uint8_t)(value | 0x7E);
    /* Only the internal clock drives a transfer when nothing is connected. */
    if ((value & 0x81) == 0x81) {
        if (serial.output) {
            fputc(serial.sb, serial.output);
            fflush(serial.output);
        }
        serial.transferring = true;
        serial.counter = SERIAL_TRANSFER_CYCLES;
    }
}
