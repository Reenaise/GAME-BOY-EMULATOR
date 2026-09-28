#ifndef GB_CPU_H
#define GB_CPU_H

#include <stdbool.h>
#include <stdint.h>

#include "registers.h"

/*
 * Instruction metadata table, modelled on Cinoop's `struct instruction`.
 *
 * Cinoop stores the disassembly string, the operand length and an execute
 * function pointer per opcode. We keep the disassembly and operand length
 * (the debugger uses them) and add explicit cycle counts. Execution itself is
 * done by a decoder in cpu.c (see docs/cpu.md for why).
 *
 * All cycle counts are in T-cycles (4.194304 MHz clock). For conditional
 * instructions `ticks` is the "not taken" cost and `ticksTaken` the cost
 * when the condition is true.
 */
struct instruction {
    const char *disassembly;
    uint8_t operandLength;
    uint8_t ticks;
    uint8_t ticksTaken;
};

extern const struct instruction instructions[256];
extern const char *const cbRegisterNames[8];

struct cpu {
    bool halted;        /* HALT: waiting for an interrupt                 */
    bool stopped;       /* STOP: waiting for a joypad press               */
    bool haltBug;       /* next opcode fetch does not increment PC        */
    bool locked;        /* an illegal opcode hung the CPU                 */
    uint8_t lockedOpcode;
    int imeDelay;       /* EI enables IME after the following instruction */
    uint64_t ticks;     /* total T-cycles executed since reset            */
};

extern struct cpu cpu;

/* Reset to the state the DMG boot ROM leaves behind (PC = 0x0100). */
void cpuReset(void);
/* Reset to the power-on state (PC = 0x0000) for running a boot ROM. */
void cpuResetForBootRom(void);

/*
 * Execute one instruction (or service one interrupt, or idle for one M-cycle
 * while halted). Returns the number of T-cycles consumed.
 */
int cpuStep(void);

/* Wake the CPU from STOP mode (called when a button is pressed). */
void cpuWakeFromStop(void);

#endif
