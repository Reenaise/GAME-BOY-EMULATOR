/*
 * Console debugger.
 *
 * Cinoop's debugger shows the registers and the disassembly of the current
 * instruction (generated from the instruction table), supports stepping and
 * breakpoints on PC or memory access. This is the same idea as a small
 * command prompt:
 *
 *   s [n]        step n instructions (default 1)
 *   c            continue
 *   f            run until the next frame
 *   b XXXX       add breakpoint          d XXXX   delete breakpoint
 *   w XXXX       break on write to XXXX  l        list breakpoints
 *   r            registers               i        I/O state
 *   m XXXX [n]   hex dump memory         u [XXXX] [n] disassemble
 *   t            toggle trace to stdout
 *   q            quit the emulator       h        help
 */

#include "debugger.h"

#include <stdlib.h>
#include <string.h>

#include "cpu.h"
#include "emulator.h"
#include "interrupts.h"
#include "memory.h"
#include "ppu.h"
#include "timer.h"

struct debugger debugger;

void debuggerReset(void) {
    memset(&debugger, 0, sizeof(debugger));
}

bool debuggerAddBreakpoint(uint16_t address) {
    int i;
    for (i = 0; i < debugger.breakpointCount; i++) {
        if (debugger.breakpoints[i] == address) return true;
    }
    if (debugger.breakpointCount >= DEBUGGER_MAX_BREAKPOINTS) return false;
    debugger.breakpoints[debugger.breakpointCount++] = address;
    debugger.enabled = true;
    return true;
}

bool debuggerRemoveBreakpoint(uint16_t address) {
    int i;
    for (i = 0; i < debugger.breakpointCount; i++) {
        if (debugger.breakpoints[i] == address) {
            debugger.breakpoints[i] = debugger.breakpoints[--debugger.breakpointCount];
            return true;
        }
    }
    return false;
}

bool debuggerAddWatchpoint(uint16_t address) {
    if (debugger.watchpointCount >= DEBUGGER_MAX_WATCHPOINTS) return false;
    debugger.watchpoints[debugger.watchpointCount++] = address;
    debugger.enabled = true;
    return true;
}

bool debuggerShouldBreak(void) {
    int i;

    if (debugger.paused) return true;

    if (debugger.skipBreakpointOnce) {
        debugger.skipBreakpointOnce = false;
        return false;
    }

    for (i = 0; i < debugger.breakpointCount; i++) {
        if (debugger.breakpoints[i] == registers.pc) {
            printf("\n*** Breakpoint hit at 0x%04X\n", registers.pc);
            debugger.paused = true;
            return true;
        }
    }
    return false;
}

void debuggerOnWrite(uint16_t address, uint8_t value) {
    int i;
    for (i = 0; i < debugger.watchpointCount; i++) {
        if (debugger.watchpoints[i] == address) {
            printf("\n*** Watchpoint: write 0x%02X to 0x%04X (instruction before PC=0x%04X)\n",
                   value, address, registers.pc);
            debugger.paused = true;
        }
    }
}

/* ------------------------------------------------------------------------- */
/* Disassembly and display                                                    */
/* ------------------------------------------------------------------------- */

int debuggerDisassemble(uint16_t address, char *buf, size_t size) {
    static const char *const cbOps[8] = { "RLC", "RRC", "RL", "RR", "SLA", "SRA", "SWAP", "SRL" };
    uint8_t opcode = readByte(address);
    const struct instruction *ins = &instructions[opcode];

    if (opcode == 0xCB) {
        uint8_t cb = readByte((uint16_t)(address + 1));
        const char *reg = cbRegisterNames[cb & 7];
        int bit = (cb >> 3) & 7;
        switch (cb >> 6) {
            case 0: snprintf(buf, size, "%s %s", cbOps[bit], reg); break;
            case 1: snprintf(buf, size, "BIT %d, %s", bit, reg); break;
            case 2: snprintf(buf, size, "RES %d, %s", bit, reg); break;
            default: snprintf(buf, size, "SET %d, %s", bit, reg); break;
        }
        return 2;
    }

    if (ins->operandLength == 0) {
        snprintf(buf, size, "%s", ins->disassembly);
    } else if (ins->operandLength == 1) {
        uint8_t operand = readByte((uint16_t)(address + 1));
        if (strstr(ins->disassembly, "%+d")) {
            int offset = (int8_t)operand;
            char text[48];
            snprintf(text, sizeof(text), ins->disassembly, offset);
            if (ins->disassembly[0] == 'J') /* JR: also show the target */
                snprintf(buf, size, "%s  (-> 0x%04X)", text, (uint16_t)(address + 2 + offset));
            else
                snprintf(buf, size, "%s", text);
        } else {
            snprintf(buf, size, ins->disassembly, operand);
        }
    } else {
        snprintf(buf, size, ins->disassembly, readShort((uint16_t)(address + 1)));
    }
    return 1 + ins->operandLength;
}

void debuggerPrintRegisters(FILE *out) {
    char text[64];
    uint8_t opcode = readByte(registers.pc);
    debuggerDisassemble(registers.pc, text, sizeof(text));

    fprintf(out, "AF=%04X  BC=%04X  DE=%04X  HL=%04X  SP=%04X  PC=%04X\n",
            registers.af, registers.bc, registers.de, registers.hl, registers.sp, registers.pc);
    fprintf(out, "Flags: %c%c%c%c   IME=%d IE=%02X IF=%02X   %s%s  cycles=%llu\n",
            FLAGS_ISSET(FLAG_ZERO) ? 'Z' : '-',
            FLAGS_ISSET(FLAG_NEGATIVE) ? 'N' : '-',
            FLAGS_ISSET(FLAG_HALFCARRY) ? 'H' : '-',
            FLAGS_ISSET(FLAG_CARRY) ? 'C' : '-',
            interrupt.master, interrupt.enable, interrupt.flags,
            cpu.halted ? "HALTED " : "", cpu.stopped ? "STOPPED " : "",
            (unsigned long long)cpu.ticks);
    fprintf(out, "Next:  %04X: %02X  %s\n", registers.pc, opcode, text);
}

void debuggerPrintMemory(FILE *out, uint16_t address, int length) {
    int i;
    for (i = 0; i < length; i += 16) {
        int j;
        uint16_t lineAddress = (uint16_t)(address + i);
        fprintf(out, "%04X: ", lineAddress);
        for (j = 0; j < 16 && i + j < length; j++) fprintf(out, "%02X ", readByte((uint16_t)(lineAddress + j)));
        fprintf(out, "\n");
    }
}

void debuggerTraceInstruction(void) {
    char text[64];
    debuggerDisassemble(registers.pc, text, sizeof(text));
    fprintf(debugger.trace, "PC:%04X AF:%04X BC:%04X DE:%04X HL:%04X SP:%04X LY:%02X  %s\n",
            registers.pc, registers.af, registers.bc, registers.de, registers.hl, registers.sp,
            ppu.ly, text);
}

static void printIoState(void) {
    printf("LCDC=%02X STAT=%02X LY=%02X LYC=%02X SCX=%02X SCY=%02X WX=%02X WY=%02X mode=%d\n",
           ppu.lcdc, ppuRead(0xFF41), ppu.ly, ppu.lyc, ppu.scx, ppu.scy, ppu.wx, ppu.wy, ppu.mode);
    printf("BGP=%02X OBP0=%02X OBP1=%02X  DIV=%02X TIMA=%02X TMA=%02X TAC=%02X\n",
           ppu.bgp, ppu.obp0, ppu.obp1, timerRead(0xFF04), timer.tima, timer.tma, timer.tac);
    printf("IE=%02X IF=%02X IME=%d  P1=%02X\n", interrupt.enable, interrupt.flags, interrupt.master,
           readByte(0xFF00));
}

static void printHelp(void) {
    printf("Commands:\n"
           "  s [n]         step n instructions (default 1)\n"
           "  c             continue running\n"
           "  f             run one frame, then stop again\n"
           "  b XXXX        add a PC breakpoint (hex)\n"
           "  d XXXX        delete a breakpoint\n"
           "  w XXXX        break when XXXX is written\n"
           "  l             list breakpoints and watchpoints\n"
           "  r             show registers\n"
           "  i             show PPU / timer / interrupt registers\n"
           "  m XXXX [n]    dump n bytes of memory (default 64)\n"
           "  u [XXXX] [n]  disassemble n instructions (default: PC, 10)\n"
           "  t             toggle instruction trace to stdout\n"
           "  q             quit the emulator\n");
}

/* ------------------------------------------------------------------------- */
/* Prompt                                                                     */
/* ------------------------------------------------------------------------- */

bool debuggerPrompt(void) {
    char line[128];

    debugger.enabled = true;
    printf("\n");
    debuggerPrintRegisters(stdout);

    for (;;) {
        char command = 0;
        unsigned a = 0, b = 0;
        int args;

        printf("(gbdb) ");
        fflush(stdout);
        if (!fgets(line, sizeof(line), stdin)) return false; /* EOF: quit */

        args = sscanf(line, " %c %x %x", &command, &a, &b);
        if (args <= 0) command = 's'; /* empty line = step, like many debuggers */

        switch (command) {
            case 's': {
                unsigned count = 1; /* the step count is decimal, addresses are hex */
                unsigned i;
                if (sscanf(line, " %*c %u", &count) != 1 || count == 0) count = 1;
                for (i = 0; i < count; i++) {
                    if (count <= 20 || i + 1 == count) {
                        char text[64];
                        debuggerDisassemble(registers.pc, text, sizeof(text));
                        printf("%04X: %s\n", registers.pc, text);
                    }
                    emulatorStep();
                }
                debuggerPrintRegisters(stdout);
                break;
            }
            case 'c':
                debugger.paused = false;
                debugger.skipBreakpointOnce = true;
                return true;
            case 'f':
                debugger.paused = false;
                debugger.skipBreakpointOnce = true;
                emulatorRunFrame();
                debugger.paused = true;
                debuggerPrintRegisters(stdout);
                break;
            case 'b':
                if (args < 2) { printf("usage: b XXXX\n"); break; }
                if (debuggerAddBreakpoint((uint16_t)a)) printf("Breakpoint at 0x%04X\n", a & 0xFFFF);
                else printf("Too many breakpoints\n");
                break;
            case 'd':
                if (args < 2) { printf("usage: d XXXX\n"); break; }
                printf(debuggerRemoveBreakpoint((uint16_t)a) ? "Removed\n" : "No such breakpoint\n");
                break;
            case 'w':
                if (args < 2) { printf("usage: w XXXX\n"); break; }
                if (debuggerAddWatchpoint((uint16_t)a)) printf("Watching writes to 0x%04X\n", a & 0xFFFF);
                else printf("Too many watchpoints\n");
                break;
            case 'l': {
                int i;
                printf("Breakpoints:");
                for (i = 0; i < debugger.breakpointCount; i++) printf(" %04X", debugger.breakpoints[i]);
                printf("\nWatchpoints:");
                for (i = 0; i < debugger.watchpointCount; i++) printf(" %04X", debugger.watchpoints[i]);
                printf("\n");
                break;
            }
            case 'r':
                debuggerPrintRegisters(stdout);
                break;
            case 'i':
                printIoState();
                break;
            case 'm':
                if (args < 2) { printf("usage: m XXXX [n]\n"); break; }
                debuggerPrintMemory(stdout, (uint16_t)a, args >= 3 ? (int)b : 64);
                break;
            case 'u': {
                uint16_t address = args >= 2 ? (uint16_t)a : registers.pc;
                int count = args >= 3 ? (int)b : 10;
                int i;
                for (i = 0; i < count; i++) {
                    char text[64];
                    int length = debuggerDisassemble(address, text, sizeof(text));
                    printf("%04X: %s\n", address, text);
                    address = (uint16_t)(address + length);
                }
                break;
            }
            case 't':
                debugger.trace = debugger.trace ? NULL : stdout;
                printf("Trace %s\n", debugger.trace ? "on" : "off");
                break;
            case 'q':
                return false;
            case 'h':
            case '?':
                printHelp();
                break;
            default:
                printf("Unknown command '%c' (h for help)\n", command);
                break;
        }
    }
}
