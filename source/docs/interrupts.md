# Interrupts

Files: `include/interrupts.h`, `src/interrupts.c`, and the HALT/EI handling in `src/cpu.c`

## Registers

| Name | Where | Meaning |
|------|-------|---------|
| IE | FFFF | Which interrupts may be serviced |
| IF | FF0F | Which interrupts are requested (bits 5–7 read as 1) |
| IME | internal | Master enable, set by EI and RETI, cleared by DI and on dispatch |

## Sources, priority and vectors

| Bit | Interrupt | Vector | Raised by |
|-----|-----------|--------|-----------|
| 0 | V-Blank | 0040 | PPU, when LY becomes 144 |
| 1 | LCD STAT | 0048 | PPU, on a rising edge of the STAT line |
| 2 | Timer | 0050 | Timer, one M-cycle after TIMA overflows |
| 3 | Serial | 0058 | Serial, when a transfer completes |
| 4 | Joypad | 0060 | Input, when a selected button line goes low |

A lower bit has higher priority.

## Dispatch

This happens at the start of `cpuStep()`, before any instruction is fetched:

```
if IME and (IE & IF & 0x1F):
    pick the lowest set bit
    IME = 0
    clear that IF bit
    push PC
    PC = vector
    20 cycles
```

## Timing details

- **EI** enables IME only after the next instruction, so `EI; RET` returns
  before any interrupt can run. `EI; DI` leaves interrupts disabled.
- **RETI** sets IME immediately.
- **HALT** idles until `IE & IF != 0`:
  - with IME=1, the interrupt is serviced and returns to the instruction
    after HALT;
  - with IME=0, execution simply continues after HALT;
  - if an interrupt is already pending when HALT runs with IME=0, the
    **HALT bug** occurs: the next byte is executed twice.
- **STOP** is ended by a joypad press.

## Tests

`tests/test_interrupts.c` covers the vectors, the full dispatch sequence,
priority, IE masking, IME=0, RETI, the EI delay followed by an interrupt, and
HALT wake-up. `test_cpu.c` covers the HALT bug. The timer and PPU suites
check that each component requests its interrupt.
