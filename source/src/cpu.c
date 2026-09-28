/*
 * Sharp LR35902 (DMG CPU) interpreter.
 *
 * Structure follows Cinoop's cpu.c: fetch the opcode at PC, look it up in the
 * instruction table, read its operands, execute it and account its cycles.
 *
 * Cinoop has one C function per opcode. Here the opcodes that share a
 * regular bit pattern (LD r,r' / ALU A,r / INC r / DEC r / LD r,n / CB xx)
 * are decoded from their bits instead, which keeps the file readable and
 * makes every register variant share the same, tested, flag logic.
 *
 * Register index used by the decoder (bits 0-2 or 3-5 of the opcode):
 *   0=B 1=C 2=D 3=E 4=H 5=L 6=(HL) 7=A
 */

#include "cpu.h"

#include "interrupts.h"
#include "memory.h"
#include "timer.h"

struct registers registers;
struct cpu cpu;

/* ------------------------------------------------------------------------- */
/* Reset                                                                      */
/* ------------------------------------------------------------------------- */

void cpuReset(void) {
    /* Values left by the DMG boot ROM (Pan Docs "Power Up Sequence"). */
    registers.af = 0x01B0;
    registers.bc = 0x0013;
    registers.de = 0x00D8;
    registers.hl = 0x014D;
    registers.sp = 0xFFFE;
    registers.pc = 0x0100;

    cpu.halted = false;
    cpu.stopped = false;
    cpu.haltBug = false;
    cpu.locked = false;
    cpu.lockedOpcode = 0;
    cpu.imeDelay = 0;
    cpu.ticks = 0;
}

void cpuResetForBootRom(void) {
    cpuReset();
    registers.af = registers.bc = registers.de = registers.hl = 0;
    registers.sp = 0;
    registers.pc = 0;
}

void cpuWakeFromStop(void) {
    cpu.stopped = false;
}

/* ------------------------------------------------------------------------- */
/* Helpers                                                                    */
/* ------------------------------------------------------------------------- */

static uint8_t fetch8(void) {
    return readByte(registers.pc++);
}

static uint16_t fetch16(void) {
    uint16_t value = readShort(registers.pc);
    registers.pc += 2;
    return value;
}

static uint8_t getRegister(int index) {
    switch (index) {
        case 0: return registers.b;
        case 1: return registers.c;
        case 2: return registers.d;
        case 3: return registers.e;
        case 4: return registers.h;
        case 5: return registers.l;
        case 6: return readByte(registers.hl);
        default: return registers.a;
    }
}

static void setRegister(int index, uint8_t value) {
    switch (index) {
        case 0: registers.b = value; break;
        case 1: registers.c = value; break;
        case 2: registers.d = value; break;
        case 3: registers.e = value; break;
        case 4: registers.h = value; break;
        case 5: registers.l = value; break;
        case 6: writeByte(registers.hl, value); break;
        default: registers.a = value; break;
    }
}

/* 16-bit pair for LD rr,nn / INC rr / DEC rr / ADD HL,rr: BC DE HL SP */
static uint16_t *registerPair(int index) {
    switch (index) {
        case 0: return &registers.bc;
        case 1: return &registers.de;
        case 2: return &registers.hl;
        default: return &registers.sp;
    }
}

/* Condition codes: 0=NZ 1=Z 2=NC 3=C */
static bool condition(int index) {
    switch (index) {
        case 0: return !FLAGS_ISSET(FLAG_ZERO);
        case 1: return FLAGS_ISSET(FLAG_ZERO) != 0;
        case 2: return !FLAGS_ISSET(FLAG_CARRY);
        default: return FLAGS_ISSET(FLAG_CARRY) != 0;
    }
}

static void setFlags(bool z, bool n, bool h, bool c) {
    registers.f = (uint8_t)((z ? FLAG_ZERO : 0) | (n ? FLAG_NEGATIVE : 0) |
                            (h ? FLAG_HALFCARRY : 0) | (c ? FLAG_CARRY : 0));
}

/* ------------------------------------------------------------------------- */
/* 8-bit arithmetic / logic                                                   */
/* ------------------------------------------------------------------------- */

static void add8(uint8_t value, bool withCarry) {
    unsigned carry = (withCarry && FLAGS_ISSET(FLAG_CARRY)) ? 1 : 0;
    unsigned result = registers.a + value + carry;
    bool half = ((registers.a & 0x0F) + (value & 0x0F) + carry) > 0x0F;
    registers.a = (uint8_t)result;
    setFlags(registers.a == 0, false, half, result > 0xFF);
}

/* SUB/SBC/CP. `store` is false for CP, which only sets the flags. */
static void sub8(uint8_t value, bool withCarry, bool store) {
    int carry = (withCarry && FLAGS_ISSET(FLAG_CARRY)) ? 1 : 0;
    int result = registers.a - value - carry;
    bool half = ((registers.a & 0x0F) - (value & 0x0F) - carry) < 0;
    setFlags((uint8_t)result == 0, true, half, result < 0);
    if (store) registers.a = (uint8_t)result;
}

static void alu(int operation, uint8_t value) {
    switch (operation) {
        case 0: add8(value, false); break;                   /* ADD */
        case 1: add8(value, true); break;                    /* ADC */
        case 2: sub8(value, false, true); break;             /* SUB */
        case 3: sub8(value, true, true); break;              /* SBC */
        case 4: registers.a &= value;                        /* AND */
                setFlags(registers.a == 0, false, true, false); break;
        case 5: registers.a ^= value;                        /* XOR */
                setFlags(registers.a == 0, false, false, false); break;
        case 6: registers.a |= value;                        /* OR  */
                setFlags(registers.a == 0, false, false, false); break;
        default: sub8(value, false, false); break;           /* CP  */
    }
}

static uint8_t inc8(uint8_t value) {
    uint8_t result = (uint8_t)(value + 1);
    /* Carry is not affected by INC. */
    registers.f = (uint8_t)((registers.f & FLAG_CARRY) |
                            (result == 0 ? FLAG_ZERO : 0) |
                            ((value & 0x0F) == 0x0F ? FLAG_HALFCARRY : 0));
    return result;
}

static uint8_t dec8(uint8_t value) {
    uint8_t result = (uint8_t)(value - 1);
    registers.f = (uint8_t)((registers.f & FLAG_CARRY) | FLAG_NEGATIVE |
                            (result == 0 ? FLAG_ZERO : 0) |
                            ((value & 0x0F) == 0x00 ? FLAG_HALFCARRY : 0));
    return result;
}

/* ADD HL,rr: Z unaffected, H from bit 11, C from bit 15. */
static void addHL(uint16_t value) {
    unsigned result = registers.hl + value;
    bool half = ((registers.hl & 0x0FFF) + (value & 0x0FFF)) > 0x0FFF;
    registers.f = (uint8_t)((registers.f & FLAG_ZERO) |
                            (half ? FLAG_HALFCARRY : 0) |
                            (result > 0xFFFF ? FLAG_CARRY : 0));
    registers.hl = (uint16_t)result;
}

/*
 * SP + signed 8-bit offset (ADD SP,e and LD HL,SP+e).
 * Flags come from the unsigned addition of the LOW byte of SP and the
 * offset byte, Z and N are always cleared.
 */
static uint16_t addSPOffset(uint8_t offset) {
    int8_t signedOffset = (int8_t)offset;
    bool half = ((registers.sp & 0x0F) + (offset & 0x0F)) > 0x0F;
    bool carry = ((registers.sp & 0xFF) + offset) > 0xFF;
    setFlags(false, false, half, carry);
    return (uint16_t)(registers.sp + signedOffset);
}

/*
 * DAA: turn the result of the last BCD addition/subtraction back into BCD.
 * Cinoop's author found this one confusing; this is the well-known correct
 * version that uses N, H and C from the previous operation.
 */
static void daa(void) {
    uint8_t a = registers.a;
    bool carry = FLAGS_ISSET(FLAG_CARRY) != 0;

    if (!FLAGS_ISSET(FLAG_NEGATIVE)) {
        if (carry || a > 0x99) { a += 0x60; carry = true; }
        if (FLAGS_ISSET(FLAG_HALFCARRY) || (a & 0x0F) > 0x09) a += 0x06;
    } else {
        if (carry) a -= 0x60;
        if (FLAGS_ISSET(FLAG_HALFCARRY)) a -= 0x06;
    }

    registers.a = a;
    registers.f = (uint8_t)((registers.f & FLAG_NEGATIVE) |
                            (a == 0 ? FLAG_ZERO : 0) |
                            (carry ? FLAG_CARRY : 0));
}

/* ------------------------------------------------------------------------- */
/* Stack and control flow                                                     */
/* ------------------------------------------------------------------------- */

static void push(uint16_t value) {
    writeShortToStack(value);
}

static uint16_t pop(void) {
    return readShortFromStack();
}

static void call(uint16_t address) {
    push(registers.pc);
    registers.pc = address;
}

/* ------------------------------------------------------------------------- */
/* CB-prefixed instructions                                                   */
/* ------------------------------------------------------------------------- */

static uint8_t rotateShift(int operation, uint8_t value) {
    bool carryIn = FLAGS_ISSET(FLAG_CARRY) != 0;
    bool carryOut;
    uint8_t result;

    switch (operation) {
        case 0: /* RLC */
            carryOut = value & 0x80;
            result = (uint8_t)((value << 1) | (value >> 7));
            break;
        case 1: /* RRC */
            carryOut = value & 0x01;
            result = (uint8_t)((value >> 1) | (value << 7));
            break;
        case 2: /* RL (through carry) */
            carryOut = value & 0x80;
            result = (uint8_t)((value << 1) | (carryIn ? 1 : 0));
            break;
        case 3: /* RR (through carry) */
            carryOut = value & 0x01;
            result = (uint8_t)((value >> 1) | (carryIn ? 0x80 : 0));
            break;
        case 4: /* SLA */
            carryOut = value & 0x80;
            result = (uint8_t)(value << 1);
            break;
        case 5: /* SRA (bit 7 kept) */
            carryOut = value & 0x01;
            result = (uint8_t)((value >> 1) | (value & 0x80));
            break;
        case 6: /* SWAP nibbles */
            carryOut = false;
            result = (uint8_t)((value << 4) | (value >> 4));
            break;
        default: /* SRL */
            carryOut = value & 0x01;
            result = (uint8_t)(value >> 1);
            break;
    }

    setFlags(result == 0, false, false, carryOut);
    return result;
}

static int executeCB(void) {
    uint8_t opcode = fetch8();
    int reg = opcode & 7;
    int bit = (opcode >> 3) & 7;
    bool memory = (reg == 6);
    uint8_t value = getRegister(reg);

    switch (opcode >> 6) {
        case 0: /* rotates / shifts / swap */
            setRegister(reg, rotateShift(bit, value));
            break;
        case 1: /* BIT b, r: Z = !bit, N = 0, H = 1, C unchanged */
            registers.f = (uint8_t)((registers.f & FLAG_CARRY) | FLAG_HALFCARRY |
                                    ((value & (1 << bit)) ? 0 : FLAG_ZERO));
            return memory ? 12 : 8;
        case 2: /* RES b, r */
            setRegister(reg, (uint8_t)(value & ~(1 << bit)));
            break;
        default: /* SET b, r */
            setRegister(reg, (uint8_t)(value | (1 << bit)));
            break;
    }

    return memory ? 16 : 8;
}

/* ------------------------------------------------------------------------- */
/* Main decoder                                                               */
/* ------------------------------------------------------------------------- */

/* Executes `opcode` (PC already points past it). Returns T-cycles. */
static int execute(uint8_t opcode) {
    const struct instruction *info = &instructions[opcode];
    int reg = (opcode >> 3) & 7; /* destination / operation field */
    int src = opcode & 7;        /* source register field         */

    /* ---- Regular blocks ---------------------------------------------- */

    if (opcode >= 0x40 && opcode <= 0x7F && opcode != 0x76) { /* LD r, r' */
        setRegister(reg, getRegister(src));
        return info->ticks;
    }

    if (opcode >= 0x80 && opcode <= 0xBF) { /* ALU A, r */
        alu(reg, getRegister(src));
        return info->ticks;
    }

    if (opcode < 0x40) {
        switch (opcode & 0x07) {
            case 0x04: setRegister(reg, inc8(getRegister(reg))); return info->ticks; /* INC r */
            case 0x05: setRegister(reg, dec8(getRegister(reg))); return info->ticks; /* DEC r */
            case 0x06: setRegister(reg, fetch8()); return info->ticks;              /* LD r, n */
            default: break;
        }
        switch (opcode & 0x0F) {
            case 0x01: *registerPair(opcode >> 4) = fetch16(); return info->ticks;   /* LD rr, nn */
            case 0x03: (*registerPair(opcode >> 4))++; return info->ticks;          /* INC rr */
            case 0x09: addHL(*registerPair(opcode >> 4)); return info->ticks;       /* ADD HL, rr */
            case 0x0B: (*registerPair(opcode >> 4))--; return info->ticks;          /* DEC rr */
            default: break;
        }
    }

    /* ---- Everything else, one case per opcode ------------------------ */

    switch (opcode) {
        case 0x00: /* NOP */
            break;

        /* Loads through register pairs */
        case 0x02: writeByte(registers.bc, registers.a); break;
        case 0x12: writeByte(registers.de, registers.a); break;
        case 0x22: writeByte(registers.hl++, registers.a); break;
        case 0x32: writeByte(registers.hl--, registers.a); break;
        case 0x0A: registers.a = readByte(registers.bc); break;
        case 0x1A: registers.a = readByte(registers.de); break;
        case 0x2A: registers.a = readByte(registers.hl++); break;
        case 0x3A: registers.a = readByte(registers.hl--); break;

        case 0x08: /* LD (nn), SP */
            writeShort(fetch16(), registers.sp);
            break;

        /* Rotates on A: like the CB versions but Z is always cleared */
        case 0x07: registers.a = rotateShift(0, registers.a); FLAGS_CLEAR(FLAG_ZERO); break; /* RLCA */
        case 0x0F: registers.a = rotateShift(1, registers.a); FLAGS_CLEAR(FLAG_ZERO); break; /* RRCA */
        case 0x17: registers.a = rotateShift(2, registers.a); FLAGS_CLEAR(FLAG_ZERO); break; /* RLA  */
        case 0x1F: registers.a = rotateShift(3, registers.a); FLAGS_CLEAR(FLAG_ZERO); break; /* RRA  */

        case 0x10: /* STOP 0 */
            fetch8();
            cpu.stopped = true;
            timerWrite(0xFF04, 0); /* STOP resets DIV */
            break;

        /* Relative jumps */
        case 0x18: {
            int8_t offset = (int8_t)fetch8();
            registers.pc = (uint16_t)(registers.pc + offset);
            break;
        }
        case 0x20: case 0x28: case 0x30: case 0x38: {
            int8_t offset = (int8_t)fetch8();
            if (condition((opcode >> 3) & 3)) {
                registers.pc = (uint16_t)(registers.pc + offset);
                return info->ticksTaken;
            }
            break;
        }

        /* Misc arithmetic on A / flags */
        case 0x27: daa(); break;
        case 0x2F: /* CPL */
            registers.a = (uint8_t)~registers.a;
            FLAGS_SET(FLAG_NEGATIVE | FLAG_HALFCARRY);
            break;
        case 0x37: /* SCF */
            registers.f = (uint8_t)((registers.f & FLAG_ZERO) | FLAG_CARRY);
            break;
        case 0x3F: /* CCF */
            registers.f = (uint8_t)((registers.f & (FLAG_ZERO | FLAG_CARRY)) ^ FLAG_CARRY);
            break;

        case 0x76: /* HALT */
            if (!interrupt.master && interruptPending()) {
                /* HALT bug: CPU does not halt and the next byte is read twice. */
                cpu.haltBug = true;
            } else {
                cpu.halted = true;
            }
            break;

        /* Returns */
        case 0xC0: case 0xC8: case 0xD0: case 0xD8:
            if (condition((opcode >> 3) & 3)) {
                registers.pc = pop();
                return info->ticksTaken;
            }
            break;
        case 0xC9: registers.pc = pop(); break;                 /* RET  */
        case 0xD9: registers.pc = pop(); interrupt.master = true; break; /* RETI */

        /* Stack */
        case 0xC1: registers.bc = pop(); break;
        case 0xD1: registers.de = pop(); break;
        case 0xE1: registers.hl = pop(); break;
        case 0xF1: registers.af = (uint16_t)(pop() & 0xFFF0); break; /* low nibble of F is always 0 */
        case 0xC5: push(registers.bc); break;
        case 0xD5: push(registers.de); break;
        case 0xE5: push(registers.hl); break;
        case 0xF5: push(registers.af); break;

        /* Absolute jumps */
        case 0xC2: case 0xCA: case 0xD2: case 0xDA: {
            uint16_t address = fetch16();
            if (condition((opcode >> 3) & 3)) {
                registers.pc = address;
                return info->ticksTaken;
            }
            break;
        }
        case 0xC3: registers.pc = fetch16(); break;
        case 0xE9: registers.pc = registers.hl; break;

        /* Calls */
        case 0xC4: case 0xCC: case 0xD4: case 0xDC: {
            uint16_t address = fetch16();
            if (condition((opcode >> 3) & 3)) {
                call(address);
                return info->ticksTaken;
            }
            break;
        }
        case 0xCD: {
            uint16_t address = fetch16();
            call(address);
            break;
        }

        /* Restarts */
        case 0xC7: case 0xCF: case 0xD7: case 0xDF:
        case 0xE7: case 0xEF: case 0xF7: case 0xFF:
            call(opcode & 0x38);
            break;

        /* ALU A, n */
        case 0xC6: case 0xCE: case 0xD6: case 0xDE:
        case 0xE6: case 0xEE: case 0xF6: case 0xFE:
            alu(reg, fetch8());
            break;

        case 0xCB:
            return executeCB();

        /* High-page loads */
        case 0xE0: writeByte((uint16_t)(0xFF00 + fetch8()), registers.a); break;
        case 0xF0: registers.a = readByte((uint16_t)(0xFF00 + fetch8())); break;
        case 0xE2: writeByte((uint16_t)(0xFF00 + registers.c), registers.a); break;
        case 0xF2: registers.a = readByte((uint16_t)(0xFF00 + registers.c)); break;
        case 0xEA: writeByte(fetch16(), registers.a); break;
        case 0xFA: registers.a = readByte(fetch16()); break;

        /* 16-bit SP arithmetic */
        case 0xE8: registers.sp = addSPOffset(fetch8()); break;
        case 0xF8: registers.hl = addSPOffset(fetch8()); break;
        case 0xF9: registers.sp = registers.hl; break;

        /* Interrupt control */
        case 0xF3: /* DI */
            interrupt.master = false;
            cpu.imeDelay = 0;
            break;
        case 0xFB: /* EI: takes effect after the next instruction */
            if (!interrupt.master && cpu.imeDelay == 0) cpu.imeDelay = 2;
            break;

        default:
            /* D3 DB DD E3 E4 EB EC ED F4 FC FD: the real CPU hangs. */
            cpu.locked = true;
            cpu.lockedOpcode = opcode;
            registers.pc--;
            break;
    }

    return info->ticks;
}

/* ------------------------------------------------------------------------- */
/* Step                                                                       */
/* ------------------------------------------------------------------------- */

int cpuStep(void) {
    int cycles;

    if (cpu.locked || cpu.stopped) {
        cpu.ticks += 4;
        return 4;
    }

    if (cpu.halted) {
        /* HALT ends as soon as an enabled interrupt is requested, even when
         * IME is 0 (then execution simply continues after HALT). */
        if (!interruptPending()) {
            cpu.ticks += 4;
            return 4;
        }
        cpu.halted = false;
    }

    cycles = interruptStep();
    if (cycles) {
        cpu.ticks += (uint64_t)cycles;
        return cycles;
    }

    {
        uint8_t opcode = readByte(registers.pc);
        if (cpu.haltBug) cpu.haltBug = false; /* PC fails to increment once */
        else registers.pc++;
        cycles = execute(opcode);
    }

    /* EI's delayed enable: IME becomes 1 after the instruction following EI. */
    if (cpu.imeDelay > 0 && --cpu.imeDelay == 0) interrupt.master = true;

    cpu.ticks += (uint64_t)cycles;
    return cycles;
}
