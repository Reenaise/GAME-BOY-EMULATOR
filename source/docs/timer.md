# Timer

Files: `include/timer.h`, `src/timer.c`

Cinoop does not emulate the timer. It returns `rand()` for DIV because Tetris
only uses DIV as a source of randomness. This project emulates the real
hardware instead, because many games rely on the timer interrupt.

## Registers

| Address | Name | Behaviour |
|---------|------|-----------|
| FF04 | DIV | Upper 8 bits of a 16-bit counter that increments every T-cycle. Any write resets the whole counter |
| FF05 | TIMA | Counter clocked at the rate TAC selects |
| FF06 | TMA | Value loaded into TIMA after an overflow |
| FF07 | TAC | Bit 2 enables the timer. Bits 0–1 select the clock. Bits 3–7 read as 1 |

| TAC & 3 | Frequency | Cycles per tick | Counter bit watched |
|---------|-----------|-----------------|---------------------|
| 0 | 4096 Hz | 1024 | 9 |
| 1 | 262144 Hz | 16 | 3 |
| 2 | 65536 Hz | 64 | 5 |
| 3 | 16384 Hz | 256 | 7 |

DIV itself increases at 16384 Hz.

## How TIMA is clocked

The hardware computes a signal equal to `TAC.enable AND counter[bit]` and
increments TIMA on each **falling edge** of that signal. The emulator does
exactly the same, which also reproduces these known side effects:

- writing DIV while the watched bit is 1 increments TIMA;
- changing TAC so that the signal drops increments TIMA.

## Overflow

When TIMA overflows from 0xFF:

1. TIMA reads 0x00 for one M-cycle (4 T-cycles).
2. Then TIMA = TMA and the timer interrupt (IF bit 2) is requested.

Writing TIMA during that M-cycle cancels the reload and the interrupt.

## Tests

`tests/test_timer.c` checks the DIV rate and reset, all four TIMA
frequencies, disabled counting, overflow, reload and the interrupt, the
reload cancel, the DIV-write falling edge, and a CPU program that HALTs until
the timer interrupt arrives. The demo ROM's seconds counter is driven by the
timer interrupt, and the integration test checks it.
