/*
 * Instruction metadata table (Cinoop style).
 *
 * { disassembly, operand length, cycles, cycles when a condition is taken }
 *
 * Operand formats used by the debugger:
 *   0x%02X  8-bit immediate      0x%04X  16-bit immediate
 *   %+d     signed 8-bit offset (JR, ADD SP,e, LD HL,SP+e)
 */

#include "cpu.h"

#define R8(prefix, cyc, cycHL) \
    { prefix "B", 0, cyc, cyc }, { prefix "C", 0, cyc, cyc }, \
    { prefix "D", 0, cyc, cyc }, { prefix "E", 0, cyc, cyc }, \
    { prefix "H", 0, cyc, cyc }, { prefix "L", 0, cyc, cyc }, \
    { prefix "(HL)", 0, cycHL, cycHL }, { prefix "A", 0, cyc, cyc }

#define ILLEGAL { "ILLEGAL", 0, 4, 4 }

const struct instruction instructions[256] = {
    /* 0x00 */
    { "NOP", 0, 4, 4 },
    { "LD BC, 0x%04X", 2, 12, 12 },
    { "LD (BC), A", 0, 8, 8 },
    { "INC BC", 0, 8, 8 },
    { "INC B", 0, 4, 4 },
    { "DEC B", 0, 4, 4 },
    { "LD B, 0x%02X", 1, 8, 8 },
    { "RLCA", 0, 4, 4 },
    { "LD (0x%04X), SP", 2, 20, 20 },
    { "ADD HL, BC", 0, 8, 8 },
    { "LD A, (BC)", 0, 8, 8 },
    { "DEC BC", 0, 8, 8 },
    { "INC C", 0, 4, 4 },
    { "DEC C", 0, 4, 4 },
    { "LD C, 0x%02X", 1, 8, 8 },
    { "RRCA", 0, 4, 4 },

    /* 0x10 */
    { "STOP", 1, 4, 4 },
    { "LD DE, 0x%04X", 2, 12, 12 },
    { "LD (DE), A", 0, 8, 8 },
    { "INC DE", 0, 8, 8 },
    { "INC D", 0, 4, 4 },
    { "DEC D", 0, 4, 4 },
    { "LD D, 0x%02X", 1, 8, 8 },
    { "RLA", 0, 4, 4 },
    { "JR %+d", 1, 12, 12 },
    { "ADD HL, DE", 0, 8, 8 },
    { "LD A, (DE)", 0, 8, 8 },
    { "DEC DE", 0, 8, 8 },
    { "INC E", 0, 4, 4 },
    { "DEC E", 0, 4, 4 },
    { "LD E, 0x%02X", 1, 8, 8 },
    { "RRA", 0, 4, 4 },

    /* 0x20 */
    { "JR NZ, %+d", 1, 8, 12 },
    { "LD HL, 0x%04X", 2, 12, 12 },
    { "LD (HL+), A", 0, 8, 8 },
    { "INC HL", 0, 8, 8 },
    { "INC H", 0, 4, 4 },
    { "DEC H", 0, 4, 4 },
    { "LD H, 0x%02X", 1, 8, 8 },
    { "DAA", 0, 4, 4 },
    { "JR Z, %+d", 1, 8, 12 },
    { "ADD HL, HL", 0, 8, 8 },
    { "LD A, (HL+)", 0, 8, 8 },
    { "DEC HL", 0, 8, 8 },
    { "INC L", 0, 4, 4 },
    { "DEC L", 0, 4, 4 },
    { "LD L, 0x%02X", 1, 8, 8 },
    { "CPL", 0, 4, 4 },

    /* 0x30 */
    { "JR NC, %+d", 1, 8, 12 },
    { "LD SP, 0x%04X", 2, 12, 12 },
    { "LD (HL-), A", 0, 8, 8 },
    { "INC SP", 0, 8, 8 },
    { "INC (HL)", 0, 12, 12 },
    { "DEC (HL)", 0, 12, 12 },
    { "LD (HL), 0x%02X", 1, 12, 12 },
    { "SCF", 0, 4, 4 },
    { "JR C, %+d", 1, 8, 12 },
    { "ADD HL, SP", 0, 8, 8 },
    { "LD A, (HL-)", 0, 8, 8 },
    { "DEC SP", 0, 8, 8 },
    { "INC A", 0, 4, 4 },
    { "DEC A", 0, 4, 4 },
    { "LD A, 0x%02X", 1, 8, 8 },
    { "CCF", 0, 4, 4 },

    /* 0x40 - 0x7F: LD r, r' */
    R8("LD B, ", 4, 8),
    R8("LD C, ", 4, 8),
    R8("LD D, ", 4, 8),
    R8("LD E, ", 4, 8),
    R8("LD H, ", 4, 8),
    R8("LD L, ", 4, 8),
    { "LD (HL), B", 0, 8, 8 },
    { "LD (HL), C", 0, 8, 8 },
    { "LD (HL), D", 0, 8, 8 },
    { "LD (HL), E", 0, 8, 8 },
    { "LD (HL), H", 0, 8, 8 },
    { "LD (HL), L", 0, 8, 8 },
    { "HALT", 0, 4, 4 },
    { "LD (HL), A", 0, 8, 8 },
    R8("LD A, ", 4, 8),

    /* 0x80 - 0xBF: 8-bit ALU with register operand */
    R8("ADD A, ", 4, 8),
    R8("ADC A, ", 4, 8),
    R8("SUB ", 4, 8),
    R8("SBC A, ", 4, 8),
    R8("AND ", 4, 8),
    R8("XOR ", 4, 8),
    R8("OR ", 4, 8),
    R8("CP ", 4, 8),

    /* 0xC0 */
    { "RET NZ", 0, 8, 20 },
    { "POP BC", 0, 12, 12 },
    { "JP NZ, 0x%04X", 2, 12, 16 },
    { "JP 0x%04X", 2, 16, 16 },
    { "CALL NZ, 0x%04X", 2, 12, 24 },
    { "PUSH BC", 0, 16, 16 },
    { "ADD A, 0x%02X", 1, 8, 8 },
    { "RST 0x00", 0, 16, 16 },
    { "RET Z", 0, 8, 20 },
    { "RET", 0, 16, 16 },
    { "JP Z, 0x%04X", 2, 12, 16 },
    { "CB 0x%02X", 1, 0, 0 },      /* cycles come from the CB decoder */
    { "CALL Z, 0x%04X", 2, 12, 24 },
    { "CALL 0x%04X", 2, 24, 24 },
    { "ADC A, 0x%02X", 1, 8, 8 },
    { "RST 0x08", 0, 16, 16 },

    /* 0xD0 */
    { "RET NC", 0, 8, 20 },
    { "POP DE", 0, 12, 12 },
    { "JP NC, 0x%04X", 2, 12, 16 },
    ILLEGAL,
    { "CALL NC, 0x%04X", 2, 12, 24 },
    { "PUSH DE", 0, 16, 16 },
    { "SUB 0x%02X", 1, 8, 8 },
    { "RST 0x10", 0, 16, 16 },
    { "RET C", 0, 8, 20 },
    { "RETI", 0, 16, 16 },
    { "JP C, 0x%04X", 2, 12, 16 },
    ILLEGAL,
    { "CALL C, 0x%04X", 2, 12, 24 },
    ILLEGAL,
    { "SBC A, 0x%02X", 1, 8, 8 },
    { "RST 0x18", 0, 16, 16 },

    /* 0xE0 */
    { "LDH (0xFF00+0x%02X), A", 1, 12, 12 },
    { "POP HL", 0, 12, 12 },
    { "LD (0xFF00+C), A", 0, 8, 8 },
    ILLEGAL,
    ILLEGAL,
    { "PUSH HL", 0, 16, 16 },
    { "AND 0x%02X", 1, 8, 8 },
    { "RST 0x20", 0, 16, 16 },
    { "ADD SP, %+d", 1, 16, 16 },
    { "JP HL", 0, 4, 4 },
    { "LD (0x%04X), A", 2, 16, 16 },
    ILLEGAL,
    ILLEGAL,
    ILLEGAL,
    { "XOR 0x%02X", 1, 8, 8 },
    { "RST 0x28", 0, 16, 16 },

    /* 0xF0 */
    { "LDH A, (0xFF00+0x%02X)", 1, 12, 12 },
    { "POP AF", 0, 12, 12 },
    { "LD A, (0xFF00+C)", 0, 8, 8 },
    { "DI", 0, 4, 4 },
    ILLEGAL,
    { "PUSH AF", 0, 16, 16 },
    { "OR 0x%02X", 1, 8, 8 },
    { "RST 0x30", 0, 16, 16 },
    { "LD HL, SP%+d", 1, 12, 12 },
    { "LD SP, HL", 0, 8, 8 },
    { "LD A, (0x%04X)", 2, 16, 16 },
    { "EI", 0, 4, 4 },
    ILLEGAL,
    ILLEGAL,
    { "CP 0x%02X", 1, 8, 8 },
    { "RST 0x38", 0, 16, 16 },
};

/* Used to build CB-prefixed disassembly: "<op> <bit>, <reg>". */
const char *const cbRegisterNames[8] = { "B", "C", "D", "E", "H", "L", "(HL)", "A" };
