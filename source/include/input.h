#ifndef GB_INPUT_H
#define GB_INPUT_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Joypad (P1, FF00). Cinoop calls this module "keys".
 *
 * Bit 5 low selects the action buttons, bit 4 low selects the d-pad.
 * The low nibble reads 0 for a pressed button (active low).
 */
enum button {
    BUTTON_RIGHT = 0, BUTTON_LEFT, BUTTON_UP, BUTTON_DOWN, /* d-pad    */
    BUTTON_A, BUTTON_B, BUTTON_SELECT, BUTTON_START,       /* actions  */
    BUTTON_COUNT
};

struct input {
    uint8_t select;   /* bits 4-5 as last written to P1          */
    uint8_t pressed;  /* bit n set = enum button n is held down   */
};

extern struct input input;

void inputReset(void);
void inputSetButton(enum button button, bool pressed);
uint8_t inputRead(void);
void inputWrite(uint8_t value);

const char *buttonName(enum button button);

#endif
