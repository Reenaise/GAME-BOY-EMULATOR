/*
 * Joypad register P1 (FF00). Cinoop: keys.c.
 *
 *   bit 5  0 = select action buttons (Start, Select, B, A)
 *   bit 4  0 = select direction keys (Down, Up, Left, Right)
 *   bit 3  Down  / Start    \
 *   bit 2  Up    / Select    |  0 = pressed
 *   bit 1  Left  / B         |
 *   bit 0  Right / A        /
 *
 * The joypad interrupt is requested when any of the low four lines goes
 * from high to low (a newly pressed button in a selected group).
 */

#include "input.h"

#include "cpu.h"
#include "interrupts.h"

struct input input;

void inputReset(void) {
    input.select = 0x30;
    input.pressed = 0;
}

static uint8_t lowNibble(void) {
    uint8_t lines = 0x0F;
    if (!(input.select & 0x10)) lines &= (uint8_t)~(input.pressed & 0x0F);        /* d-pad   */
    if (!(input.select & 0x20)) lines &= (uint8_t)~((input.pressed >> 4) & 0x0F); /* buttons */
    return lines;
}

uint8_t inputRead(void) {
    return (uint8_t)(0xC0 | input.select | lowNibble());
}

void inputWrite(uint8_t value) {
    uint8_t before = lowNibble();
    input.select = value & 0x30;
    /* Selecting a group with a held button also pulls a line low. */
    if (before & ~lowNibble() & 0x0F) requestInterrupt(INTERRUPT_JOYPAD);
}

void inputSetButton(enum button button, bool pressed) {
    uint8_t before;
    uint8_t mask;

    if (button >= BUTTON_COUNT) return;
    before = lowNibble();
    mask = (uint8_t)(1u << button);

    if (pressed) input.pressed |= mask;
    else input.pressed &= (uint8_t)~mask;

    if (pressed) {
        if (before & ~lowNibble() & 0x0F) requestInterrupt(INTERRUPT_JOYPAD);
        cpuWakeFromStop(); /* any button press ends STOP mode */
    }
}

const char *buttonName(enum button button) {
    static const char *const names[BUTTON_COUNT] = {
        "Right", "Left", "Up", "Down", "A", "B", "Select", "Start"
    };
    return button < BUTTON_COUNT ? names[button] : "?";
}
