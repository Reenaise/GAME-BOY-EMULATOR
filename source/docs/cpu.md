# CPU (Sharp LR35902)

Files: `include/registers.h`, `include/cpu.h`, `src/cpu.c`, `src/instructions.c`

## Registers

```c
registers.a  registers.f   <->  registers.af
registers.b  registers.c   <->  registers.bc
registers.d  registers.e   <->  registers.de
registers.h  registers.l   <->  registers.hl
registers.sp registers.pc
```

This is Cinoop's C11 anonymous union trick. It relies on a little-endian host.

The flag register F contains:

| Bit | Flag | Meaning |
|-----|------|---------|
| 7 | Z | result was zero |
| 6 | N | last operation was a subtraction (used by DAA) |
| 5 | H | carry from bit 3 to 4 (bit 11 to 12 for 16-bit ADD) |
| 4 | C | carry from bit 7 (bit 15), or a borrow |

The low four bits of F are always 0. `POP AF` masks them off.

## Instruction table

`instructions[256]` holds `{ disassembly, operandLength, ticks, ticksTaken }`.
It is Cinoop's table, with cycles added. For example:

```c
{ "JR NZ, %+d", 1, 8, 12 }      // 8 cycles if not taken, 12 if taken
{ "LD BC, 0x%04X", 2, 12, 12 }
```

The debugger formats the operand into the disassembly string.

## Execution

`cpuStep()` does the following:

1. If the CPU is **locked** (illegal opcode) or **stopped**, it burns 4 cycles.
2. If it is **halted**, it stays halted until `IE & IF != 0`. The CPU wakes
   even if IME is 0, and in that case simply continues after HALT.
3. `interruptStep()` checks for an interrupt to service: it clears IME, pushes
   PC and jumps to the vector, costing 20 cycles.
4. Otherwise it fetches the opcode (see the **HALT bug** below), then decodes
   and executes it.
5. It applies the **EI delay**: IME becomes 1 only after the instruction that
   follows EI.

Decoding uses the opcode's bit fields:

| Opcodes | Pattern | Meaning |
|---------|---------|---------|
| 40–7F (except 76) | `01 ddd sss` | LD r, r' |
| 80–BF | `10 ooo sss` | ALU op on A (ADD ADC SUB SBC AND XOR OR CP) |
| x4 / xC in 00–3F | `00 rrr 100` | INC r |
| x5 / xD in 00–3F | `00 rrr 101` | DEC r |
| x6 / xE in 00–3F | `00 rrr 110` | LD r, n |
| CB 00–3F | `00 ooo rrr` | RLC RRC RL RR SLA SRA SWAP SRL |
| CB 40–FF | `bb bbb rrr` | BIT, RES, SET |

The register index is 0=B 1=C 2=D 3=E 4=H 5=L 6=(HL) 7=A. The remaining
opcodes are handled one by one in a `switch`.

## Flag rules worth remembering

- **INC/DEC r** never touch C. H is set when bit 3 carries (INC) or borrows
  (DEC).
- **ADD HL,rr** leaves Z unchanged. H comes from bit 11 and C from bit 15.
- **ADD SP,e** and **LD HL,SP+e** clear Z and N. H and C come from the
  *unsigned* addition of SP's low byte and the offset byte, even when the
  offset is negative.
- **RLCA/RRCA/RLA/RRA** always clear Z. Their CB versions set Z from the result.
- **AND** sets H. **OR/XOR** clear H and C.
- **BIT** sets H, clears N, keeps C, and sets Z if the bit is 0.
- **DAA** corrects A after BCD arithmetic, using N, H and C. It clears H.
- **SCF/CCF** clear N and H.

## Cycle counts

All counts are in T-cycles (4.194304 MHz). Conditional instructions take
extra cycles when the condition is true:

| Instruction | Not taken | Taken |
|-------------|-----------|-------|
| JR cc | 8 | 12 |
| JP cc | 12 | 16 |
| CALL cc | 12 | 24 |
| RET cc | 8 | 20 |

CB instructions take 8 cycles, 16 with (HL), and 12 for BIT n,(HL).

## Special behaviour

- **HALT bug:** if HALT runs with IME=0 while an interrupt is already pending,
  the CPU does not halt and the next opcode byte is read twice.
- **STOP** consumes its padding byte, resets DIV and waits for a button press.
- **Illegal opcodes** (D3 DB DD E3 E4 EB EC ED F4 FC FD) lock the CPU, as on
  hardware. The front end reports the opcode and address.

## Verification

- `tests/test_cpu.c` has 33 tests: loads, arithmetic flags, DAA, rotates,
  every CB opcode's cycle count, jumps, calls, stack, EI, HALT and STOP.
- Blargg `cpu_instrs` passes all 11 sub-tests and `instr_timing` passes.
