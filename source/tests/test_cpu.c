/* CPU tests: registers, flags, arithmetic, loads, jumps, calls, stack, CB. */

#include <string.h>

#include "cpu.h"
#include "interrupts.h"
#include "memory.h"
#include "test.h"

#define LOAD(...) do { static const uint8_t code_[] = { __VA_ARGS__ }; testLoadCode(code_, sizeof(code_)); } while (0)

/* ---- Structure ----------------------------------------------------------- */

static void instructionTableComplete(void) {
    int i;
    for (i = 0; i < 256; i++) CHECK(instructions[i].disassembly != NULL);
    CHECK(strcmp(instructions[0x00].disassembly, "NOP") == 0);
    CHECK(strcmp(instructions[0x76].disassembly, "HALT") == 0);
    CHECK(strcmp(instructions[0x7F].disassembly, "LD A, A") == 0);
    CHECK(strcmp(instructions[0xBE].disassembly, "CP (HL)") == 0);
    CHECK(strcmp(instructions[0xFF].disassembly, "RST 0x38") == 0);
    CHECK_EQ(instructions[0xC3].operandLength, 2);
    CHECK_EQ(instructions[0x3E].operandLength, 1);
}

static void registerPairs(void) {
    registers.af = 0x1234;
    CHECK_EQ(registers.a, 0x12);
    CHECK_EQ(registers.f, 0x34);
    registers.b = 0xAB;
    registers.c = 0xCD;
    CHECK_EQ(registers.bc, 0xABCD);
    registers.de = 0xBEEF;
    CHECK_EQ(registers.d, 0xBE);
    CHECK_EQ(registers.e, 0xEF);
    registers.h = 0x80;
    registers.l = 0x01;
    CHECK_EQ(registers.hl, 0x8001);
}

static void resetState(void) {
    cpuReset();
    CHECK_EQ(registers.af, 0x01B0);
    CHECK_EQ(registers.bc, 0x0013);
    CHECK_EQ(registers.de, 0x00D8);
    CHECK_EQ(registers.hl, 0x014D);
    CHECK_EQ(registers.sp, 0xFFFE);
    CHECK_EQ(registers.pc, 0x0100);
}

/* ---- Loads ---------------------------------------------------------------- */

static void loadImmediate(void) {
    LOAD(0x06, 0x12, 0x0E, 0x34, 0x16, 0x56, 0x1E, 0x78, 0x26, 0x9A, 0x2E, 0xBC, 0x3E, 0xDE,
         0x01, 0x34, 0x12, 0x31, 0x00, 0xD0);
    CHECK_EQ(testStep(7), 7 * 8);
    CHECK_EQ(registers.b, 0x12);
    CHECK_EQ(registers.c, 0x34);
    CHECK_EQ(registers.d, 0x56);
    CHECK_EQ(registers.e, 0x78);
    CHECK_EQ(registers.h, 0x9A);
    CHECK_EQ(registers.l, 0xBC);
    CHECK_EQ(registers.a, 0xDE);
    CHECK_EQ(testStep(1), 12); /* LD BC, nn */
    CHECK_EQ(registers.bc, 0x1234);
    testStep(1);
    CHECK_EQ(registers.sp, 0xD000);
}

static void loadRegisterToRegister(void) {
    LOAD(0x06, 0x42, 0x48, 0x51, 0x5A, 0x63, 0x6C, 0x7D);
    /* LD B,42; LD C,B; LD D,C; LD E,D; LD H,E; LD L,H; LD A,L */
    testStep(1);
    CHECK_EQ(testStep(6), 6 * 4);
    CHECK_EQ(registers.a, 0x42);
    CHECK_EQ(registers.hl, 0x4242);
}

static void loadIndirect(void) {
    /* LD HL,D000; LD (HL),77; LD A,(HL+); LD (HL-),A; LD A,(HL); LD BC,D010; LD (BC),A; LD A,(BC) */
    LOAD(0x21, 0x00, 0xD0, 0x36, 0x77, 0x2A, 0x32, 0x7E, 0x01, 0x10, 0xD0, 0x02, 0x0A);
    testStep(1);
    CHECK_EQ(testStep(1), 12);
    CHECK_EQ(readByte(0xD000), 0x77);
    testStep(1);
    CHECK_EQ(registers.a, 0x77);
    CHECK_EQ(registers.hl, 0xD001);
    testStep(1);
    CHECK_EQ(readByte(0xD001), 0x77);
    CHECK_EQ(registers.hl, 0xD000);
    CHECK_EQ(testStep(1), 8);
    CHECK_EQ(registers.a, 0x77);
    testStep(2);
    CHECK_EQ(readByte(0xD010), 0x77);
    registers.a = 0;
    testStep(1);
    CHECK_EQ(registers.a, 0x77);
}

static void loadHighPageAndAbsolute(void) {
    /* LD A,5A; LDH (80),A; LD C,81; LD (C),A; LD A,0; LDH A,(80); LD (D123),A; LD A,(D123) */
    LOAD(0x3E, 0x5A, 0xE0, 0x80, 0x0E, 0x81, 0xE2, 0x3E, 0x00, 0xF0, 0x80, 0xEA, 0x23, 0xD1, 0xFA, 0x23, 0xD1);
    testStep(1);
    CHECK_EQ(testStep(1), 12);
    CHECK_EQ(hram[0], 0x5A);
    testStep(1);
    CHECK_EQ(testStep(1), 8);
    CHECK_EQ(hram[1], 0x5A);
    testStep(2);
    CHECK_EQ(registers.a, 0x5A);
    CHECK_EQ(testStep(1), 16);
    CHECK_EQ(readByte(0xD123), 0x5A);
    registers.a = 0;
    CHECK_EQ(testStep(1), 16);
    CHECK_EQ(registers.a, 0x5A);
}

static void loadSPToMemory(void) {
    LOAD(0x31, 0x34, 0x12, 0x08, 0x00, 0xD0, 0x21, 0x00, 0xD1, 0xF9);
    testStep(1);
    CHECK_EQ(testStep(1), 20);
    CHECK_EQ(readByte(0xD000), 0x34);
    CHECK_EQ(readByte(0xD001), 0x12);
    testStep(1);
    CHECK_EQ(testStep(1), 8); /* LD SP, HL */
    CHECK_EQ(registers.sp, 0xD100);
}

/* ---- 8-bit arithmetic ------------------------------------------------------ */

static void add8Flags(void) {
    LOAD(0x3E, 0x3A, 0x06, 0xC6, 0x80);      /* LD A,3A; LD B,C6; ADD A,B */
    testStep(3);
    CHECK_EQ(registers.a, 0x00);
    CHECK_EQ(registers.f, FLAG_ZERO | FLAG_HALFCARRY | FLAG_CARRY);

    LOAD(0x3E, 0x0F, 0xC6, 0x01);            /* ADD A,1: half carry only */
    testStep(2);
    CHECK_EQ(registers.a, 0x10);
    CHECK_EQ(registers.f, FLAG_HALFCARRY);

    LOAD(0x3E, 0xF0, 0xC6, 0x20);            /* carry without half carry */
    testStep(2);
    CHECK_EQ(registers.a, 0x10);
    CHECK_EQ(registers.f, FLAG_CARRY);
}

static void adcFlags(void) {
    LOAD(0x37, 0x3E, 0xE1, 0xCE, 0x0F);      /* SCF; LD A,E1; ADC A,0F */
    testStep(3);
    CHECK_EQ(registers.a, 0xF1);
    CHECK_EQ(registers.f, FLAG_HALFCARRY);

    LOAD(0x37, 0x3E, 0xE1, 0xCE, 0x1E);      /* SCF; ADC A,1E = 0x00 with carry */
    testStep(3);
    CHECK_EQ(registers.a, 0x00);
    CHECK_EQ(registers.f, FLAG_ZERO | FLAG_HALFCARRY | FLAG_CARRY);
}

static void subAndCompare(void) {
    LOAD(0x3E, 0x3E, 0xD6, 0x3E);            /* SUB 3E */
    testStep(2);
    CHECK_EQ(registers.a, 0);
    CHECK_EQ(registers.f, FLAG_ZERO | FLAG_NEGATIVE);

    LOAD(0x3E, 0x3E, 0xD6, 0x0F);            /* half borrow */
    testStep(2);
    CHECK_EQ(registers.a, 0x2F);
    CHECK_EQ(registers.f, FLAG_NEGATIVE | FLAG_HALFCARRY);

    LOAD(0x3E, 0x3E, 0xD6, 0x40);            /* borrow */
    testStep(2);
    CHECK_EQ(registers.a, 0xFE);
    CHECK_EQ(registers.f, FLAG_NEGATIVE | FLAG_CARRY);

    LOAD(0x3E, 0x3C, 0xFE, 0x40);            /* CP 40: A unchanged */
    testStep(2);
    CHECK_EQ(registers.a, 0x3C);
    CHECK_EQ(registers.f, FLAG_NEGATIVE | FLAG_CARRY);

    LOAD(0x3E, 0x3C, 0xFE, 0x3C);            /* CP equal */
    testStep(2);
    CHECK_EQ(registers.f, FLAG_ZERO | FLAG_NEGATIVE);
}

static void sbcFlags(void) {
    LOAD(0x37, 0x3E, 0x3B, 0xDE, 0x2A);      /* SCF; LD A,3B; SBC A,2A */
    testStep(3);
    CHECK_EQ(registers.a, 0x10);
    CHECK_EQ(registers.f, FLAG_NEGATIVE);

    LOAD(0x37, 0x3E, 0x3B, 0xDE, 0x3A);      /* = 0 */
    testStep(3);
    CHECK_EQ(registers.a, 0x00);
    CHECK_EQ(registers.f, FLAG_ZERO | FLAG_NEGATIVE);

    LOAD(0x37, 0x3E, 0x3B, 0xDE, 0x4F);      /* borrow and half borrow */
    testStep(3);
    CHECK_EQ(registers.a, 0xEB);
    CHECK_EQ(registers.f, FLAG_NEGATIVE | FLAG_HALFCARRY | FLAG_CARRY);
}

static void logicOperations(void) {
    LOAD(0x3E, 0x5A, 0xE6, 0x3F);            /* AND: H always set */
    testStep(2);
    CHECK_EQ(registers.a, 0x1A);
    CHECK_EQ(registers.f, FLAG_HALFCARRY);

    LOAD(0x3E, 0x5A, 0xE6, 0x00);
    testStep(2);
    CHECK_EQ(registers.f, FLAG_ZERO | FLAG_HALFCARRY);

    LOAD(0x37, 0x3E, 0x5A, 0xF6, 0x03);      /* OR clears carry */
    testStep(3);
    CHECK_EQ(registers.a, 0x5B);
    CHECK_EQ(registers.f, 0);

    LOAD(0x37, 0xAF);                        /* XOR A */
    testStep(2);
    CHECK_EQ(registers.a, 0);
    CHECK_EQ(registers.f, FLAG_ZERO);

    LOAD(0x3E, 0xFF, 0xEE, 0x0F);
    testStep(2);
    CHECK_EQ(registers.a, 0xF0);
}

static void aluWithHLOperand(void) {
    LOAD(0x21, 0x00, 0xD0, 0x36, 0x05, 0x3E, 0x10, 0x86, 0x96, 0xBE);
    testStep(3);
    CHECK_EQ(testStep(1), 8);  /* ADD A,(HL) */
    CHECK_EQ(registers.a, 0x15);
    testStep(1);               /* SUB (HL) */
    CHECK_EQ(registers.a, 0x10);
    testStep(1);               /* CP (HL) */
    CHECK_EQ(registers.f, FLAG_NEGATIVE | FLAG_HALFCARRY);
}

static void incDec8(void) {
    LOAD(0x37, 0x3E, 0xFF, 0x3C);            /* INC A with carry set: C kept */
    testStep(3);
    CHECK_EQ(registers.a, 0x00);
    CHECK_EQ(registers.f, FLAG_ZERO | FLAG_HALFCARRY | FLAG_CARRY);

    /* After reset F = B0 (carry set): DEC must leave the carry alone. */
    LOAD(0x06, 0x01, 0x05);                  /* DEC B -> 0 */
    testStep(2);
    CHECK_EQ(registers.b, 0);
    CHECK_EQ(registers.f, FLAG_ZERO | FLAG_NEGATIVE | FLAG_CARRY);

    LOAD(0xB7, 0x0E, 0x10, 0x0D);            /* OR A (clears C); DEC C -> 0F half borrow */
    testStep(3);
    CHECK_EQ(registers.c, 0x0F);
    CHECK_EQ(registers.f, FLAG_NEGATIVE | FLAG_HALFCARRY);

    LOAD(0x21, 0x00, 0xD0, 0x36, 0x0F, 0x34, 0x35, 0x35);  /* INC (HL), DEC (HL) */
    testStep(2);
    CHECK_EQ(testStep(1), 12);
    CHECK_EQ(readByte(0xD000), 0x10);
    CHECK(FLAGS_ISSET(FLAG_HALFCARRY));
    testStep(2);
    CHECK_EQ(readByte(0xD000), 0x0E);
}

static void daaAdjust(void) {
    LOAD(0x3E, 0x45, 0xC6, 0x38, 0x27);      /* 45 + 38 = 83 (BCD) */
    testStep(3);
    CHECK_EQ(registers.a, 0x83);
    CHECK(!FLAGS_ISSET(FLAG_CARRY));

    LOAD(0x3E, 0x83, 0xD6, 0x38, 0x27);      /* 83 - 38 = 45 (BCD) */
    testStep(3);
    CHECK_EQ(registers.a, 0x45);
    CHECK(FLAGS_ISSET(FLAG_NEGATIVE));

    LOAD(0x3E, 0x99, 0xC6, 0x01, 0x27);      /* 99 + 1 = 00, carry */
    testStep(3);
    CHECK_EQ(registers.a, 0x00);
    CHECK_EQ(registers.f, FLAG_ZERO | FLAG_CARRY);
}

static void miscFlagOps(void) {
    LOAD(0x3E, 0x35, 0x2F);                  /* CPL */
    testStep(2);
    CHECK_EQ(registers.a, 0xCA);
    CHECK_EQ(registers.f & (FLAG_NEGATIVE | FLAG_HALFCARRY), FLAG_NEGATIVE | FLAG_HALFCARRY);

    LOAD(0xAF, 0x37);                        /* XOR A; SCF: Z kept */
    testStep(2);
    CHECK_EQ(registers.f, FLAG_ZERO | FLAG_CARRY);

    LOAD(0x37, 0x3F, 0x3F);                  /* CCF toggles */
    testStep(2);
    CHECK(!FLAGS_ISSET(FLAG_CARRY));
    testStep(1);
    CHECK(FLAGS_ISSET(FLAG_CARRY));
}

/* ---- 16-bit arithmetic ------------------------------------------------------ */

static void add16(void) {
    LOAD(0xAF, 0x21, 0x23, 0x8A, 0x01, 0x05, 0x06, 0x09);  /* HL=8A23 + BC=0605 */
    testStep(3);
    CHECK_EQ(testStep(1), 8);
    CHECK_EQ(registers.hl, 0x9028);
    CHECK_EQ(registers.f, FLAG_ZERO | FLAG_HALFCARRY);      /* Z unaffected */

    LOAD(0x21, 0x23, 0x8A, 0x29);                           /* ADD HL,HL */
    testStep(2);
    CHECK_EQ(registers.hl, 0x1446);
    CHECK_EQ(registers.f & 0x70, FLAG_HALFCARRY | FLAG_CARRY);
}

static void incDec16(void) {
    LOAD(0x01, 0xFF, 0xFF, 0x03, 0x11, 0x00, 0x00, 0x1B, 0x33, 0x3B);
    testStep(1);
    registers.f = 0xF0;
    CHECK_EQ(testStep(1), 8);
    CHECK_EQ(registers.bc, 0x0000);
    CHECK_EQ(registers.f, 0xF0); /* no flags affected */
    testStep(2);
    CHECK_EQ(registers.de, 0xFFFF);
    testStep(1);
    CHECK_EQ(registers.sp, 0xDFF1);
    testStep(1);
    CHECK_EQ(registers.sp, 0xDFF0);
}

static void spOffsetArithmetic(void) {
    LOAD(0x31, 0xF8, 0xFF, 0xE8, 0x02);                  /* SP=FFF8; ADD SP,2 */
    testStep(1);
    CHECK_EQ(testStep(1), 16);
    CHECK_EQ(registers.sp, 0xFFFA);
    CHECK_EQ(registers.f, 0);

    LOAD(0x31, 0xFF, 0x00, 0xE8, 0x01);                  /* 00FF + 1: H and C */
    testStep(2);
    CHECK_EQ(registers.sp, 0x0100);
    CHECK_EQ(registers.f, FLAG_HALFCARRY | FLAG_CARRY);

    LOAD(0x31, 0x10, 0x00, 0xE8, 0xFE);                  /* 0010 - 2 */
    testStep(2);
    CHECK_EQ(registers.sp, 0x000E);
    CHECK_EQ(registers.f, FLAG_CARRY);                   /* 0x10 + 0xFE > 0xFF */

    LOAD(0x31, 0x00, 0x00, 0xF8, 0xFF);                  /* LD HL,SP-1 */
    testStep(1);
    CHECK_EQ(testStep(1), 12);
    CHECK_EQ(registers.hl, 0xFFFF);
    CHECK_EQ(registers.sp, 0x0000);
    CHECK_EQ(registers.f, 0);
}

/* ---- Rotates ------------------------------------------------------------- */

static void rotateAccumulator(void) {
    LOAD(0x3E, 0x85, 0x07);                  /* RLCA */
    testStep(2);
    CHECK_EQ(registers.a, 0x0B);
    CHECK_EQ(registers.f, FLAG_CARRY);

    LOAD(0x37, 0x3E, 0x95, 0x17);            /* RLA with carry */
    testStep(3);
    CHECK_EQ(registers.a, 0x2B);
    CHECK_EQ(registers.f, FLAG_CARRY);

    LOAD(0x3E, 0x3B, 0x0F);                  /* RRCA */
    testStep(2);
    CHECK_EQ(registers.a, 0x9D);
    CHECK_EQ(registers.f, FLAG_CARRY);

    LOAD(0xAF, 0x3E, 0x81, 0x1F);            /* RRA, carry in 0 */
    testStep(3);
    CHECK_EQ(registers.a, 0x40);
    CHECK_EQ(registers.f, FLAG_CARRY);

    LOAD(0xAF, 0x07);                        /* RLCA of 0: Z is NOT set */
    testStep(2);
    CHECK_EQ(registers.f, 0);
}

/* ---- CB prefix ------------------------------------------------------------- */

static void cbRotatesAndShifts(void) {
    LOAD(0x06, 0x85, 0xCB, 0x00);            /* RLC B */
    testStep(1);
    CHECK_EQ(testStep(1), 8);
    CHECK_EQ(registers.b, 0x0B);
    CHECK_EQ(registers.f, FLAG_CARRY);

    LOAD(0x0E, 0x00, 0xCB, 0x09);            /* RRC C of 0 -> Z */
    testStep(2);
    CHECK_EQ(registers.f, FLAG_ZERO);

    LOAD(0x37, 0x16, 0x80, 0xCB, 0x12);      /* RL D: 80 with carry -> 01, C */
    testStep(3);
    CHECK_EQ(registers.d, 0x01);
    CHECK_EQ(registers.f, FLAG_CARRY);

    LOAD(0xAF, 0x1E, 0x01, 0xCB, 0x1B);      /* RR E: 01 -> 00, Z C */
    testStep(3);
    CHECK_EQ(registers.e, 0x00);
    CHECK_EQ(registers.f, FLAG_ZERO | FLAG_CARRY);

    LOAD(0x26, 0x80, 0xCB, 0x24);            /* SLA H */
    testStep(2);
    CHECK_EQ(registers.h, 0x00);
    CHECK_EQ(registers.f, FLAG_ZERO | FLAG_CARRY);

    LOAD(0x2E, 0x8A, 0xCB, 0x2D);            /* SRA L keeps bit 7 */
    testStep(2);
    CHECK_EQ(registers.l, 0xC5);
    CHECK_EQ(registers.f, 0);

    LOAD(0x3E, 0xF1, 0xCB, 0x37);            /* SWAP A */
    testStep(2);
    CHECK_EQ(registers.a, 0x1F);
    CHECK_EQ(registers.f, 0);

    LOAD(0x3E, 0x01, 0xCB, 0x3F);            /* SRL A */
    testStep(2);
    CHECK_EQ(registers.a, 0x00);
    CHECK_EQ(registers.f, FLAG_ZERO | FLAG_CARRY);
}

static void cbBitSetRes(void) {
    LOAD(0x37, 0x26, 0x80, 0xCB, 0x7C, 0xCB, 0x44);   /* BIT 7,H; BIT 0,H */
    testStep(2);
    CHECK_EQ(testStep(1), 8);
    CHECK_EQ(registers.f, FLAG_HALFCARRY | FLAG_CARRY); /* bit set: Z=0, C kept */
    testStep(1);
    CHECK_EQ(registers.f, FLAG_ZERO | FLAG_HALFCARRY | FLAG_CARRY);

    /* SET 3,(HL); BIT 3,(HL); RES 3,(HL); RES 7,A */
    LOAD(0x21, 0x00, 0xD0, 0xCB, 0xDE, 0xCB, 0x5E, 0xCB, 0x9E, 0x3E, 0xFF, 0xCB, 0xBF);
    testStep(1);
    CHECK_EQ(testStep(1), 16);
    CHECK_EQ(readByte(0xD000), 0x08);
    CHECK_EQ(testStep(1), 12); /* BIT n,(HL) is 12 cycles */
    CHECK(!FLAGS_ISSET(FLAG_ZERO));
    testStep(1);
    CHECK_EQ(readByte(0xD000), 0x00);
    testStep(2);
    CHECK_EQ(registers.a, 0x7F);
}

static void cbAllOpcodesCycleCounts(void) {
    int op;
    for (op = 0; op < 256; op++) {
        uint8_t code[2] = { 0xCB, (uint8_t)op };
        int expected = ((op & 7) != 6) ? 8 : ((op >> 6) == 1 ? 12 : 16);
        testLoadCode(code, 2);
        registers.hl = 0xD000;
        CHECK_EQ(testStep(1), expected);
        CHECK_EQ(registers.pc, 0xC002);
    }
}

/* ---- Jumps, calls, stack ------------------------------------------------ */

static void jumps(void) {
    LOAD(0xC3, 0x10, 0xC0);                  /* JP C010 */
    CHECK_EQ(testStep(1), 16);
    CHECK_EQ(registers.pc, 0xC010);

    LOAD(0x18, 0x05);                        /* JR +5 */
    CHECK_EQ(testStep(1), 12);
    CHECK_EQ(registers.pc, 0xC007);

    LOAD(0x00, 0x00, 0x18, 0xFC);            /* JR -4 -> C000 */
    testStep(3);
    CHECK_EQ(registers.pc, 0xC000);

    LOAD(0xAF, 0x20, 0x10, 0x28, 0x10);      /* XOR A (Z=1); JR NZ not taken; JR Z taken */
    testStep(1);
    CHECK_EQ(testStep(1), 8);
    CHECK_EQ(registers.pc, 0xC003);
    CHECK_EQ(testStep(1), 12);
    CHECK_EQ(registers.pc, 0xC015);

    LOAD(0x37, 0xD2, 0x00, 0xD0, 0xDA, 0x00, 0xD0);  /* SCF; JP NC not taken; JP C taken */
    testStep(1);
    CHECK_EQ(testStep(1), 12);
    CHECK_EQ(registers.pc, 0xC004);
    CHECK_EQ(testStep(1), 16);
    CHECK_EQ(registers.pc, 0xD000);

    LOAD(0x21, 0x34, 0x12, 0xE9);            /* JP HL */
    testStep(1);
    CHECK_EQ(testStep(1), 4);
    CHECK_EQ(registers.pc, 0x1234);
}

static void callsAndReturns(void) {
    /* C000: CALL C010 ; C003: NOP ... C010: RET */
    static uint8_t code[0x20];
    memset(code, 0, sizeof(code));
    code[0] = 0xCD; code[1] = 0x10; code[2] = 0xC0;
    code[0x10] = 0xC9;
    testLoadCode(code, sizeof(code));

    CHECK_EQ(testStep(1), 24);
    CHECK_EQ(registers.pc, 0xC010);
    CHECK_EQ(registers.sp, 0xDFEE);
    CHECK_EQ(readShort(0xDFEE), 0xC003);
    CHECK_EQ(testStep(1), 16);
    CHECK_EQ(registers.pc, 0xC003);
    CHECK_EQ(registers.sp, 0xDFF0);

    /* Conditional: XOR A; CALL NZ (no); CALL Z (yes); at target RET NZ (no), RET Z (yes) */
    memset(code, 0, sizeof(code));
    code[0] = 0xAF;
    code[1] = 0xC4; code[2] = 0x10; code[3] = 0xC0;
    code[4] = 0xCC; code[5] = 0x10; code[6] = 0xC0;
    code[0x10] = 0xC0;
    code[0x11] = 0xC8;
    testLoadCode(code, sizeof(code));
    testStep(1);
    CHECK_EQ(testStep(1), 12);
    CHECK_EQ(registers.pc, 0xC004);
    CHECK_EQ(testStep(1), 24);
    CHECK_EQ(registers.pc, 0xC010);
    CHECK_EQ(testStep(1), 8);
    CHECK_EQ(testStep(1), 20);
    CHECK_EQ(registers.pc, 0xC007);
}

static void restarts(void) {
    int i;
    for (i = 0; i < 8; i++) {
        uint8_t code[1] = { (uint8_t)(0xC7 + i * 8) };
        testLoadCode(code, 1);
        CHECK_EQ(testStep(1), 16);
        CHECK_EQ(registers.pc, i * 8);
        CHECK_EQ(readShort(registers.sp), 0xC001);
    }
}

static void pushPop(void) {
    LOAD(0x01, 0x34, 0x12, 0xC5, 0xD1, 0xE5, 0xF1);
    testStep(1);
    CHECK_EQ(testStep(1), 16);  /* PUSH BC */
    CHECK_EQ(registers.sp, 0xDFEE);
    CHECK_EQ(readByte(0xDFEF), 0x12);
    CHECK_EQ(readByte(0xDFEE), 0x34);
    CHECK_EQ(testStep(1), 12);  /* POP DE */
    CHECK_EQ(registers.de, 0x1234);

    /* POP AF: the low nibble of F is always 0 */
    registers.hl = 0xABCD;
    testStep(2);
    CHECK_EQ(registers.af, 0xABC0);
}

/* ---- Interrupt control, HALT, STOP, illegal ------------------------------ */

static void eiDelay(void) {
    LOAD(0xFB, 0x00, 0x00);
    testStep(1);
    CHECK(!interrupt.master); /* not yet */
    testStep(1);
    CHECK(interrupt.master);

    LOAD(0xFB, 0xF3, 0x00);   /* EI; DI -> stays disabled */
    testStep(3);
    CHECK(!interrupt.master);
}

static void haltWaitsForInterrupt(void) {
    LOAD(0x76, 0x3C);         /* HALT; INC A */
    registers.a = 0;
    interrupt.enable = INTERRUPT_TIMER;
    testStep(1);
    CHECK(cpu.halted);
    CHECK_EQ(testStep(5), 20); /* idles 4 cycles at a time */
    CHECK_EQ(registers.pc, 0xC001);
    requestInterrupt(INTERRUPT_TIMER);
    testStep(1);              /* IME=0: wakes up and continues */
    CHECK(!cpu.halted);
    CHECK_EQ(registers.a, 1);
    CHECK(interrupt.flags & INTERRUPT_TIMER); /* not serviced */
}

static void haltBug(void) {
    LOAD(0x76, 0x3C, 0x00);   /* HALT with IME=0 and a pending interrupt: INC A runs twice */
    registers.a = 0;
    interrupt.enable = INTERRUPT_VBLANK;
    interrupt.flags = INTERRUPT_VBLANK;
    testStep(1);
    CHECK(!cpu.halted);
    testStep(2);
    CHECK_EQ(registers.a, 2);
    CHECK_EQ(registers.pc, 0xC002);
}

static void stopAndIllegal(void) {
    LOAD(0x10, 0x00, 0x3C);
    testStep(1);
    CHECK(cpu.stopped);
    CHECK_EQ(registers.pc, 0xC002);
    testStep(3);
    CHECK_EQ(registers.pc, 0xC002);
    cpuWakeFromStop();
    testStep(1);
    CHECK_EQ(registers.pc, 0xC003);

    LOAD(0xD3, 0x00);
    testStep(1);
    CHECK(cpu.locked);
    CHECK_EQ(cpu.lockedOpcode, 0xD3);
    testStep(10);
    CHECK_EQ(registers.pc, 0xC000);
}

static void smallProgram(void) {
    /* Sum 1..10 in A using a DEC/JR NZ loop:
     *   LD B,10; XOR A; loop: ADD A,B; DEC B; JR NZ,loop; LD (D000),A; HALT */
    LOAD(0x06, 0x0A, 0xAF, 0x80, 0x05, 0x20, 0xFC, 0xEA, 0x00, 0xD0, 0x76);
    testStep(2 + 30 + 1);
    CHECK_EQ(readByte(0xD000), 55);
    CHECK_EQ(registers.b, 0);
}

void suiteCpu(void) {
    static const struct testCase tests[] = {
        TEST(instructionTableComplete),
        TEST(registerPairs),
        TEST(resetState),
        TEST(loadImmediate),
        TEST(loadRegisterToRegister),
        TEST(loadIndirect),
        TEST(loadHighPageAndAbsolute),
        TEST(loadSPToMemory),
        TEST(add8Flags),
        TEST(adcFlags),
        TEST(subAndCompare),
        TEST(sbcFlags),
        TEST(logicOperations),
        TEST(aluWithHLOperand),
        TEST(incDec8),
        TEST(daaAdjust),
        TEST(miscFlagOps),
        TEST(add16),
        TEST(incDec16),
        TEST(spOffsetArithmetic),
        TEST(rotateAccumulator),
        TEST(cbRotatesAndShifts),
        TEST(cbBitSetRes),
        TEST(cbAllOpcodesCycleCounts),
        TEST(jumps),
        TEST(callsAndReturns),
        TEST(restarts),
        TEST(pushPop),
        TEST(eiDelay),
        TEST(haltWaitsForInterrupt),
        TEST(haltBug),
        TEST(stopAndIllegal),
        TEST(smallProgram),
    };
    runTests("cpu", tests, sizeof(tests) / sizeof(tests[0]));
}
