/* Interrupt tests: IF/IE, IME, priority, servicing, vectors, RETI. */

#include "cpu.h"
#include "interrupts.h"
#include "memory.h"
#include "test.h"

static void setup(void) {
    static const uint8_t code[] = { 0x00, 0x00, 0x00, 0x00 }; /* NOPs at C000 */
    testLoadCode(code, sizeof(code));
}

static void vectors(void) {
    CHECK_EQ(interruptVector(INTERRUPT_VBLANK), 0x40);
    CHECK_EQ(interruptVector(INTERRUPT_LCDSTAT), 0x48);
    CHECK_EQ(interruptVector(INTERRUPT_TIMER), 0x50);
    CHECK_EQ(interruptVector(INTERRUPT_SERIAL), 0x58);
    CHECK_EQ(interruptVector(INTERRUPT_JOYPAD), 0x60);
}

static void serviceSequence(void) {
    setup();
    interrupt.master = true;
    interrupt.enable = INTERRUPT_TIMER;
    requestInterrupt(INTERRUPT_TIMER);

    CHECK_EQ(cpuStep(), 20);
    CHECK_EQ(registers.pc, 0x0050);
    CHECK(!interrupt.master);                  /* IME cleared */
    CHECK_EQ(interrupt.flags & INTERRUPT_TIMER, 0);  /* IF bit acknowledged */
    CHECK_EQ(readShort(registers.sp), 0xC000); /* return address pushed */
}

static void priority(void) {
    setup();
    interrupt.master = true;
    interrupt.enable = 0x1F;
    interrupt.flags = 0x1F;
    cpuStep();
    CHECK_EQ(registers.pc, 0x40);              /* V-Blank first */
    CHECK_EQ(interrupt.flags, 0x1E);

    setup();
    interrupt.master = true;
    interrupt.enable = 0x1F;
    interrupt.flags = INTERRUPT_JOYPAD | INTERRUPT_SERIAL;
    cpuStep();
    CHECK_EQ(registers.pc, 0x58);              /* serial beats joypad */
}

static void enableMask(void) {
    setup();
    interrupt.master = true;
    interrupt.enable = INTERRUPT_VBLANK;
    interrupt.flags = INTERRUPT_TIMER;         /* requested but not enabled */
    cpuStep();
    CHECK_EQ(registers.pc, 0xC001);
    CHECK(!interruptPending());
    interrupt.enable |= INTERRUPT_TIMER;
    CHECK(interruptPending());
}

static void imeOffBlocksService(void) {
    setup();
    interrupt.master = false;
    interrupt.enable = 0x1F;
    interrupt.flags = INTERRUPT_VBLANK;
    CHECK_EQ(cpuStep(), 4);
    CHECK_EQ(registers.pc, 0xC001);
    CHECK_EQ(interrupt.flags, INTERRUPT_VBLANK);
}

static void retiReenables(void) {
    /* Handler at 0x40 is in ROM; use a RETI placed in WRAM instead by
     * jumping there manually. */
    static const uint8_t code[] = { 0xD9 };
    testLoadCode(code, sizeof(code));
    registers.sp = 0xDFF0;
    writeShortToStack(0x1234);
    CHECK_EQ(cpuStep(), 16);
    CHECK_EQ(registers.pc, 0x1234);
    CHECK(interrupt.master);                   /* immediately, no delay */
}

static void eiThenInterrupt(void) {
    /* EI; NOP; NOP with a pending interrupt: serviced after the NOP. */
    static const uint8_t code[] = { 0xFB, 0x00, 0x00 };
    testLoadCode(code, sizeof(code));
    interrupt.enable = INTERRUPT_VBLANK;
    interrupt.flags = INTERRUPT_VBLANK;
    cpuStep();                                 /* EI */
    CHECK_EQ(registers.pc, 0xC001);
    cpuStep();                                 /* NOP runs before the interrupt */
    CHECK_EQ(registers.pc, 0xC002);
    cpuStep();                                 /* now the interrupt */
    CHECK_EQ(registers.pc, 0x40);
    CHECK_EQ(readShort(registers.sp), 0xC002);
}

static void haltWithIme(void) {
    static const uint8_t code[] = { 0x76, 0x00 };
    testLoadCode(code, sizeof(code));
    interrupt.master = true;
    interrupt.enable = INTERRUPT_TIMER;
    cpuStep();
    CHECK(cpu.halted);
    cpuStep();
    CHECK(cpu.halted);
    requestInterrupt(INTERRUPT_TIMER);
    CHECK_EQ(cpuStep(), 20);
    CHECK(!cpu.halted);
    CHECK_EQ(registers.pc, 0x50);
    CHECK_EQ(readShort(registers.sp), 0xC001); /* returns after HALT */
}

void suiteInterrupts(void) {
    static const struct testCase tests[] = {
        TEST(vectors),
        TEST(serviceSequence),
        TEST(priority),
        TEST(enableMask),
        TEST(imeOffBlocksService),
        TEST(retiReenables),
        TEST(eiThenInterrupt),
        TEST(haltWithIme),
    };
    runTests("interrupts", tests, sizeof(tests) / sizeof(tests[0]));
}
