/*
 * Timer: DIV (FF04), TIMA (FF05), TMA (FF06), TAC (FF07).
 *
 * Not covered by the Cinoop article (it returns rand() for DIV), so this
 * follows the hardware:
 *   - a 16-bit counter increments every T-cycle; DIV is its high byte
 *   - TIMA increments on a falling edge of (TAC enable AND counter bit N),
 *     where N = 9, 3, 5, 7 for TAC clock select 0..3
 *     (4096, 262144, 65536, 16384 Hz)
 *   - on overflow TIMA reads 0 for one M-cycle, then is reloaded from TMA
 *     and the timer interrupt is requested
 *   - writing DIV clears the whole counter (which can itself cause an edge)
 */

#include "timer.h"

#include <stdbool.h>

#include "interrupts.h"

struct timer timer;

static const uint16_t timerBit[4] = { 1u << 9, 1u << 3, 1u << 5, 1u << 7 };

static bool timerSignal(void) {
    return (timer.tac & 0x04) && (timer.divCounter & timerBit[timer.tac & 0x03]);
}

static void incrementTima(void) {
    if (timer.tima == 0xFF) {
        timer.tima = 0x00;
        timer.reloadDelay = 4;
    } else {
        timer.tima++;
    }
}

/* Change the counter and TAC together, detecting a falling edge. */
static void updateCounter(uint16_t counter, uint8_t tac) {
    bool before = timerSignal();
    timer.divCounter = counter;
    timer.tac = tac;
    if (before && !timerSignal()) incrementTima();
}

void timerReset(void) {
    timer.divCounter = 0xABCC; /* DIV = 0xAB after the DMG boot ROM */
    timer.tima = 0;
    timer.tma = 0;
    timer.tac = 0xF8;
    timer.reloadDelay = 0;
}

void timerStep(int cycles) {
    /* Advance one M-cycle (4 T-cycles) at a time. */
    while (cycles > 0) {
        if (timer.reloadDelay > 0) {
            timer.reloadDelay -= 4;
            if (timer.reloadDelay <= 0) {
                timer.reloadDelay = 0;
                timer.tima = timer.tma;
                requestInterrupt(INTERRUPT_TIMER);
            }
        }
        updateCounter((uint16_t)(timer.divCounter + 4), timer.tac);
        cycles -= 4;
    }
}

uint8_t timerRead(uint16_t address) {
    switch (address) {
        case 0xFF04: return (uint8_t)(timer.divCounter >> 8);
        case 0xFF05: return timer.tima;
        case 0xFF06: return timer.tma;
        case 0xFF07: return (uint8_t)(timer.tac | 0xF8);
        default:     return 0xFF;
    }
}

void timerWrite(uint16_t address, uint8_t value) {
    switch (address) {
        case 0xFF04: /* any write resets the divider */
            updateCounter(0, timer.tac);
            break;
        case 0xFF05:
            timer.tima = value;
            timer.reloadDelay = 0; /* writing during the delay cancels the reload */
            break;
        case 0xFF06:
            timer.tma = value;
            break;
        case 0xFF07:
            updateCounter(timer.divCounter, (uint8_t)(value | 0xF8));
            break;
        default:
            break;
    }
}

unsigned timerFrequency(uint8_t tac) {
    static const unsigned frequency[4] = { 4096, 262144, 65536, 16384 };
    return frequency[tac & 0x03];
}
