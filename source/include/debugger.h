#ifndef GB_DEBUGGER_H
#define GB_DEBUGGER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/*
 * A small console debugger in the spirit of Cinoop's debug.c: register view,
 * disassembly, stepping, PC breakpoints, memory-write watchpoints, tracing.
 */

#define DEBUGGER_MAX_BREAKPOINTS 32
#define DEBUGGER_MAX_WATCHPOINTS 16

struct debugger {
    bool enabled;          /* check breakpoints while running          */
    bool paused;           /* stop before the next instruction         */
    uint16_t breakpoints[DEBUGGER_MAX_BREAKPOINTS];
    int breakpointCount;
    uint16_t watchpoints[DEBUGGER_MAX_WATCHPOINTS];
    int watchpointCount;
    bool skipBreakpointOnce; /* resume from a breakpoint without re-hitting */
    FILE *trace;           /* instruction trace output, or NULL        */
};

extern struct debugger debugger;

void debuggerReset(void);
/* Called by the emulator before every instruction. True = stop now. */
bool debuggerShouldBreak(void);
/* Called by the memory bus on writes when watchpoints exist. */
void debuggerOnWrite(uint16_t address, uint8_t value);
/* Write one trace line for the instruction at PC. */
void debuggerTraceInstruction(void);

bool debuggerAddBreakpoint(uint16_t address);
bool debuggerRemoveBreakpoint(uint16_t address);
bool debuggerAddWatchpoint(uint16_t address);

/* Disassemble the instruction at `address` into buf; returns its length. */
int debuggerDisassemble(uint16_t address, char *buf, size_t size);
void debuggerPrintRegisters(FILE *out);
void debuggerPrintMemory(FILE *out, uint16_t address, int length);

/*
 * Interactive prompt on stdin/stdout. Returns false if the user asked to
 * quit the emulator, true to continue running.
 */
bool debuggerPrompt(void);

#endif
