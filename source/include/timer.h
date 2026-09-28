#ifndef GB_TIMER_H
#define GB_TIMER_H

#include <stdint.h>

/*
 * DIV / TIMA / TMA / TAC.
 *
 * Cinoop returns rand() for DIV. We model the real hardware instead: a 16-bit
 * internal counter increments every T-cycle, DIV is its upper byte, and TIMA
 * increments on a falling edge of the counter bit selected by TAC.
 */
struct timer {
    uint16_t divCounter; /* internal system counter, DIV = divCounter >> 8 */
    uint8_t tima;
    uint8_t tma;
    uint8_t tac;
    int reloadDelay;     /* cycles until TIMA is reloaded after overflow   */
};

extern struct timer timer;

void timerReset(void);
void timerStep(int cycles);
uint8_t timerRead(uint16_t address);
void timerWrite(uint16_t address, uint8_t value);

/* TIMA input clock in Hz for a given TAC value (for docs/tests). */
unsigned timerFrequency(uint8_t tac);

#endif
