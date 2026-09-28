#ifndef GB_REGISTERS_H
#define GB_REGISTERS_H

#include <stdint.h>

/*
 * CPU registers.
 *
 * Straight from the Cinoop design: C11 anonymous structs/unions let us access
 * each register either as an 8-bit value (registers.a, registers.f) or as a
 * 16-bit pair (registers.af) without shifting/masking.
 *
 * This layout assumes a little-endian host (x86/x64/ARM), where the low byte
 * of a 16-bit pair is stored first. That is why F comes before A, C before B,
 * and so on.
 */
struct registers {
    union {
        struct { uint8_t f; uint8_t a; };
        uint16_t af;
    };
    union {
        struct { uint8_t c; uint8_t b; };
        uint16_t bc;
    };
    union {
        struct { uint8_t e; uint8_t d; };
        uint16_t de;
    };
    union {
        struct { uint8_t l; uint8_t h; };
        uint16_t hl;
    };
    uint16_t sp;
    uint16_t pc;
};

extern struct registers registers;

/* Flag bits in F. The low nibble of F is always zero on real hardware. */
#define FLAG_ZERO       0x80 /* Z: result was zero                 */
#define FLAG_NEGATIVE   0x40 /* N: last operation was a subtraction */
#define FLAG_HALFCARRY  0x20 /* H: carry out of bit 3 (or bit 11)   */
#define FLAG_CARRY      0x10 /* C: carry out of bit 7 (or bit 15)   */

#define FLAGS_ISSET(x)  (registers.f & (x))
#define FLAGS_SET(x)    (registers.f |= (x))
#define FLAGS_CLEAR(x)  (registers.f &= (uint8_t)~(x))

#endif
