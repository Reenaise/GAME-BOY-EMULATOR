/* Timer tests: DIV, TIMA frequencies, overflow/reload, interrupt. */

#include "cpu.h"
#include "interrupts.h"
#include "memory.h"
#include "test.h"
#include "timer.h"

static void setup(void) {
    testResetMachine();
    timer.divCounter = 0;
    interrupt.flags = 0;
}

static void divIncrements(void) {
    setup();
    CHECK_EQ(readByte(0xFF04), 0);
    timerStep(252);
    CHECK_EQ(readByte(0xFF04), 0);
    timerStep(4);
    CHECK_EQ(readByte(0xFF04), 1);             /* every 256 cycles = 16384 Hz */
    timerStep(256 * 10);
    CHECK_EQ(readByte(0xFF04), 11);
    writeByte(0xFF04, 0x99);                   /* any write resets */
    CHECK_EQ(readByte(0xFF04), 0);
    CHECK_EQ(timer.divCounter, 0);
}

static void timaFrequencies(void) {
    /* TAC clock select -> cycles per TIMA increment */
    static const struct { uint8_t tac; int period; unsigned hz; } cases[] = {
        { 0x04, 1024, 4096 }, { 0x05, 16, 262144 }, { 0x06, 64, 65536 }, { 0x07, 256, 16384 },
    };
    size_t i;
    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        setup();
        writeByte(0xFF07, cases[i].tac);
        writeByte(0xFF05, 0);
        timerStep(cases[i].period * 10);
        CHECK_EQ(readByte(0xFF05), 10);
        CHECK_EQ(timerFrequency(cases[i].tac), cases[i].hz);
        CHECK_EQ(cases[i].period * (int)cases[i].hz, 4194304);
    }
}

static void timerDisabled(void) {
    setup();
    writeByte(0xFF07, 0x01);                   /* fast clock, but disabled */
    timerStep(1000);
    CHECK_EQ(readByte(0xFF05), 0);
    CHECK_EQ(readByte(0xFF07), 0xF9);          /* unused bits read 1 */
}

static void overflowReloadsAndInterrupts(void) {
    setup();
    writeByte(0xFF06, 0x42);                   /* TMA */
    writeByte(0xFF05, 0xFF);                   /* TIMA */
    writeByte(0xFF07, 0x05);                   /* 16 cycles per tick */
    timerStep(16);
    CHECK_EQ(readByte(0xFF05), 0x00);          /* reads 0 for one M-cycle */
    CHECK_EQ(interrupt.flags & INTERRUPT_TIMER, 0);
    timerStep(4);
    CHECK_EQ(readByte(0xFF05), 0x42);          /* reloaded from TMA */
    CHECK(interrupt.flags & INTERRUPT_TIMER);  /* interrupt requested */
}

static void writeDuringReloadCancels(void) {
    setup();
    writeByte(0xFF06, 0x42);
    writeByte(0xFF05, 0xFF);
    writeByte(0xFF07, 0x05);
    timerStep(16);
    writeByte(0xFF05, 0x10);
    timerStep(4);
    CHECK_EQ(readByte(0xFF05), 0x10);
    CHECK_EQ(interrupt.flags & INTERRUPT_TIMER, 0);
}

static void divResetFallingEdge(void) {
    setup();
    writeByte(0xFF07, 0x05);                   /* watches counter bit 3 */
    timerStep(8);                              /* bit 3 now set */
    CHECK_EQ(readByte(0xFF05), 0);
    writeByte(0xFF04, 0);                      /* bit 3 falls -> TIMA ticks */
    CHECK_EQ(readByte(0xFF05), 1);
}

static void interruptFiresThroughCpu(void) {
    /* A real program: set up the timer, enable its interrupt and HALT. */
    static const uint8_t code[] = {
        0x3E, 0xFE, 0xE0, 0x05,   /* LD A,FE; LDH (TIMA),A */
        0x3E, 0x05, 0xE0, 0x07,   /* LD A,05; LDH (TAC),A  */
        0x3E, 0x04, 0xE0, 0xFF,   /* LD A,04; LDH (IE),A   */
        0xFB, 0x76, 0x00,         /* EI; HALT; NOP         */
    };
    int i;
    testLoadCode(code, sizeof(code));
    for (i = 0; i < 200 && registers.pc != 0x0050; i++) {
        int cycles = cpuStep();
        timerStep(cycles);
    }
    CHECK_EQ(registers.pc, 0x0050);            /* timer vector reached */
}

void suiteTimer(void) {
    static const struct testCase tests[] = {
        TEST(divIncrements),
        TEST(timaFrequencies),
        TEST(timerDisabled),
        TEST(overflowReloadsAndInterrupts),
        TEST(writeDuringReloadCancels),
        TEST(divResetFallingEdge),
        TEST(interruptFiresThroughCpu),
    };
    runTests("timer", tests, sizeof(tests) / sizeof(tests[0]));
}
