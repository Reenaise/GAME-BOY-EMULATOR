# Testing

## Running

```bash
cmake --build build
```

```bash
ctest --test-dir build --output-on-failure
```

You can also run the test binary directly, for one suite or for all of them:

```bash
build/gb_tests.exe cpu
```

```bash
build/gb_tests.exe
```

## Framework

`tests/test.h` is a tiny framework with no dependencies. It provides the
`CHECK(cond)` and `CHECK_EQ(actual, expected)` macros; a failed check prints
both values in hex. Each suite is a list of `TEST(fn)` entries, and CTest runs
each suite as a separate test.

The helpers in `tests/test_util.c` do the following:

- `testBuildRom` creates an in-memory ROM with a valid logo and checksum.
- `testResetMachine` loads a blank ROM and resets everything.
- `testLoadCode(bytes)` copies machine code to WRAM at C000 and sets PC
  there, so a CPU test is just a list of opcodes.
- `testStep(n)` executes n instructions and returns the cycles used.

## Suites

| Suite | Tests | What is covered |
|-------|-------|-----------------|
| cpu | 33 | Registers and pairs, reset state, all load forms, ADD/ADC/SUB/SBC/CP/AND/OR/XOR flags, INC/DEC, DAA, CPL/SCF/CCF, 16-bit ADD, INC/DEC rr, SP+e, rotates, CB rotates, shifts, SWAP, BIT, SET, RES, cycle counts of all 256 CB opcodes, JP/JR/CALL/RET/RST (taken and not taken), PUSH/POP AF masking, EI delay, HALT, HALT bug, STOP, illegal opcodes, a loop program |
| memory | 10 | WRAM and echo, VRAM, OAM, unusable area, HRAM and IE, 16-bit access, ROM read-only, I/O routing, sound registers are safe, OAM DMA, boot ROM overlay |
| cartridge | 11 | Header parsing and checksum, ROM only, rejecting garbage, too-small, corrupt and unsupported files, loading from a file and the `.sav` path, padding, MBC1 ROM banking, 0→1, 0x20→0x21, mode 1, masking, RAM enable and banks |
| interrupts | 8 | Vectors, dispatch (IME, IF, push, 20 cycles), priority, IE mask, IME=0, RETI, EI then interrupt, HALT with IME |
| timer | 7 | DIV rate and reset, 4 frequencies, disabled, overflow and reload with IF, reload cancel, DIV falling edge, interrupt through the CPU |
| ppu | 20 | Mode sequence, 70224-cycle frame, large steps, LY/LYC and STAT interrupts, STAT write mask, LCD on/off, BG tiles and palettes, scrolling and wrap, signed tile data, both maps, window and its line counter, BG disable, sprites (transparency, flips, OBP1, priority, X ordering, 10-per-line limit, 8×16), front buffer at V-Blank |
| input | 7 | P1 selection, d-pad and buttons (active low), both groups, joypad interrupt edge, STOP wake-up, upper bits |
| integration | 3 | A V-Blank interrupt counter program (60 frames), `roms/mbc1_test.gb` (serial output `MBC1 PASS`), `roms/demo.gb` (MBC1 self-check, text rendering, sprite pixel, joypad moves the sprite, A inverts BGP, timer-driven seconds) |

The total is 1471 checks.

## Homebrew ROMs

`tools/make_test_roms.py` builds `roms/demo.gb` and `roms/mbc1_test.gb` from
Python, using a tiny built-in assembler, so no external toolchain is needed.
Run `python tools/make_test_roms.py` to regenerate them.

## Third-party test ROMs (optional)

Blargg's test ROMs are not part of the repository. To use them, download
`cpu_instrs.gb` and `instr_timing.gb` (for example from
github.com/retrio/gb-test-roms), put them in `build/test-roms/`, and re-run
CMake. They are then added as the CTest entries `blargg_cpu_instrs` and
`blargg_instr_timing`, which pass when the serial output says "Passed".

Current results:

```
cpu_instrs

01:ok  02:ok  03:ok  04:ok  05:ok  06:ok  07:ok  08:ok  09:ok  10:ok  11:ok

Passed all tests

instr_timing

Passed
```

## Manual checks

- `gameboy-emulator roms/demo.gb` opens a window. Arrows move the smiley,
  X inverts the palette, Z flips the sprite and Enter scrolls the background.
- `gameboy-emulator --debug roms/demo.gb` starts the console debugger.
- Error handling was checked for a missing file, a non-ROM file, an unknown
  option and a bad `--scale` value.
