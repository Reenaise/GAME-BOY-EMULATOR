/* Joypad tests: P1 register selection, active-low bits, interrupt. */

#include "cpu.h"
#include "input.h"
#include "interrupts.h"
#include "memory.h"
#include "test.h"

static void setup(void) {
    testResetMachine();
    interrupt.flags = 0;
}

static void nothingPressed(void) {
    setup();
    CHECK_EQ(readByte(0xFF00), 0xFF);
    writeByte(0xFF00, 0x20);                   /* select d-pad */
    CHECK_EQ(readByte(0xFF00), 0xEF);
    writeByte(0xFF00, 0x10);                   /* select buttons */
    CHECK_EQ(readByte(0xFF00), 0xDF);
}

static void directionKeys(void) {
    setup();
    writeByte(0xFF00, 0x20);
    inputSetButton(BUTTON_RIGHT, true);
    CHECK_EQ(readByte(0xFF00) & 0x0F, 0x0E);
    inputSetButton(BUTTON_DOWN, true);
    CHECK_EQ(readByte(0xFF00) & 0x0F, 0x06);
    inputSetButton(BUTTON_RIGHT, false);
    CHECK_EQ(readByte(0xFF00) & 0x0F, 0x07);
    inputSetButton(BUTTON_DOWN, false);
    CHECK_EQ(readByte(0xFF00) & 0x0F, 0x0F);
}

static void actionButtons(void) {
    setup();
    inputSetButton(BUTTON_A, true);
    inputSetButton(BUTTON_START, true);
    writeByte(0xFF00, 0x20);                   /* d-pad selected: buttons hidden */
    CHECK_EQ(readByte(0xFF00) & 0x0F, 0x0F);
    writeByte(0xFF00, 0x10);
    CHECK_EQ(readByte(0xFF00) & 0x0F, 0x06);   /* A = bit 0, Start = bit 3 */
    inputSetButton(BUTTON_B, true);
    inputSetButton(BUTTON_SELECT, true);
    CHECK_EQ(readByte(0xFF00) & 0x0F, 0x00);
}

static void bothGroupsSelected(void) {
    setup();
    writeByte(0xFF00, 0x00);
    inputSetButton(BUTTON_LEFT, true);         /* bit 1 */
    inputSetButton(BUTTON_SELECT, true);       /* bit 2 */
    CHECK_EQ(readByte(0xFF00) & 0x0F, 0x09);
}

static void joypadInterrupt(void) {
    setup();
    writeByte(0xFF00, 0x10);                   /* buttons selected */
    inputSetButton(BUTTON_UP, true);           /* d-pad not selected: no edge */
    CHECK_EQ(interrupt.flags & INTERRUPT_JOYPAD, 0);
    inputSetButton(BUTTON_A, true);
    CHECK(interrupt.flags & INTERRUPT_JOYPAD);
    interrupt.flags = 0;
    inputSetButton(BUTTON_A, false);           /* release: no interrupt */
    CHECK_EQ(interrupt.flags & INTERRUPT_JOYPAD, 0);
}

static void pressWakesStop(void) {
    static const uint8_t code[] = { 0x10, 0x00, 0x00 };
    testLoadCode(code, sizeof(code));
    cpuStep();
    CHECK(cpu.stopped);
    inputSetButton(BUTTON_START, true);
    CHECK(!cpu.stopped);
}

static void upperBitsReadOne(void) {
    setup();
    writeByte(0xFF00, 0x00);
    CHECK_EQ(readByte(0xFF00) & 0xC0, 0xC0);
}

void suiteInput(void) {
    static const struct testCase tests[] = {
        TEST(nothingPressed),
        TEST(directionKeys),
        TEST(actionButtons),
        TEST(bothGroupsSelected),
        TEST(joypadInterrupt),
        TEST(pressWakesStop),
        TEST(upperBitsReadOne),
    };
    runTests("input", tests, sizeof(tests) / sizeof(tests[0]));
}
