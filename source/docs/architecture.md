# Architecture

## Overview

The emulator is split into a **core library** (`gbcore`, with no SDL
dependency) and a **front end** (`main.c` and `display.c`, which use SDL2).
The unit tests link only the core, so they run without a window.

Each hardware component keeps its state in a global struct, named the same way
as in Cinoop:

```c
struct registers registers;   // cpu.c
struct cpu cpu;               // cpu.c    (halted, stopped, IME delay, cycles)
struct interrupt interrupt;   // interrupts.c
struct timer timer;           // timer.c
struct ppu ppu;               // ppu.c
struct input input;           // input.c
struct cartridge cartridge;   // cartridge.c
uint8_t vram[], wram[], oam[], hram[];   // memory.c
```

Each module has a `xxxReset()` function. `emulatorReset()` calls all of them,
which makes every test start from a known state.

## The main loop

Cinoop's loop is "execute one instruction, then step the GPU by the same
number of ticks". `emulatorStep()` does the same, with the timer and serial
port added:

```
cycles = cpuStep();      // services a pending interrupt, or runs one
                         // instruction, or idles 4 cycles in HALT
timerStep(cycles);       // DIV/TIMA, may request INTERRUPT_TIMER
serialStep(cycles);      // may request INTERRUPT_SERIAL
ppuStep(cycles);         // modes/LY, draws scanlines, may request
                         // VBLANK / LCDSTAT, publishes the frame
```

`emulatorRunFrame()` repeats this until the PPU sets `frameReady` at the start
of V-Blank. That happens every 70224 cycles. If the LCD is off, it stops after
70224 cycles instead. The front end then:

1. handles SDL events and updates the joypad;
2. uploads `ppuFrontBuffer` (160×144 shade indices) to an SDL texture;
3. sleeps until the next 1/59.73 s boundary.

Interrupts are checked at the start of every `cpuStep()`, so an interrupt
requested by the timer or PPU during one instruction is serviced before the
next one, as on hardware.

## Memory bus

Every access goes through `readByte()` and `writeByte()` in `memory.c`. These
functions decode the address and forward it: cartridge (ROM, MBC registers,
external RAM), VRAM, WRAM and its echo, OAM, I/O (dispatched to input, serial,
timer, interrupts, PPU and sound storage), HRAM, and IE. See
[memory.md](memory.md).

## Frame buffers

The PPU renders into `ppuBackBuffer`. When LY reaches 144 it copies the
finished frame to `ppuFrontBuffer`. The display and screenshots read only the
front buffer, so they never show a frame that is half drawn. This is the same
idea as Cinoop's DS port, which copies its buffers during V-Blank.

## What comes from where

### Taken directly from the Cinoop guide

- The project structure: one module each for the CPU, memory, GPU (PPU),
  interrupts, keys (input), ROM (cartridge), display and debug.
- The register union built from C11 anonymous structs, with the same names.
- An instruction table of `{disassembly, operandLength, ...}` that drives the
  debugger's disassembly.
- The main loop: execute one instruction and step the GPU by its ticks.
- The GPU as a mode state machine using 80, 172, 204 and 456 cycle periods,
  rendering one scanline at a time.
- The background map row calculation `((line + scrollY) & 255) >> 3`
  (Cinoop's fix for its background bug).
- Starting at PC=0x0100 with the register values the boot ROM leaves behind.
- The default keyboard layout: Z/X/Enter/Backspace/arrows, Space for the
  debugger and Esc to quit.
- A debugger with a register view, stepping and breakpoints on PC or memory
  writes.
- No sound. Cinoop has no sound either.

### Required for correctness (where Cinoop was incomplete or wrong)

| Cinoop | This project |
|--------|--------------|
| DIV returns `rand()` | A 16-bit internal counter. TIMA is clocked by falling edges, and overflow reloads after one M-cycle ([timer.md](timer.md)) |
| Blocked writes to 0xFF80 as a Tetris-specific patch | No game-specific code anywhere |
| First V-Blank implementation was wrong | IF/IE/IME, priority, EI delay, HALT wake-up and the HALT bug, RETI |
| No mappers, 32 KiB ROMs only | MBC1: ROM banking, RAM banking, mode select, bank-0 remap, masking |
| No window, 8×8 sprites only | Window with its own line counter, 8×16 sprites, 10 sprites per line, DMG X-priority, BG-over-OBJ flag |
| DAA was a source of confusion | DAA based on N, H and C, verified by Blargg `cpu_instrs` test 01 |
| No STAT interrupts | Rising edge of the combined STAT line (LYC, mode 0, mode 1, mode 2), with the coincidence flag |
| No timing for conditional instructions | Separate taken and not-taken cycle counts, verified by Blargg `instr_timing` |

### Implementation decisions made in this project

- **Decoding by bit pattern instead of 512 functions.** Cinoop has one C
  function per opcode. Here the regular blocks (LD r,r', ALU A,r, INC r,
  DEC r, LD r,n, 16-bit ops and all of CB) are decoded from the opcode bits,
  and a `switch` handles the rest. The metadata table stays Cinoop-shaped.
- **No tile cache.** Cinoop keeps pre-decoded tiles and updates them on every
  VRAM write. We decode straight from VRAM while drawing, which is simpler
  and fast enough: headless mode runs at roughly 20× real time.
- **Instant OAM DMA**, and VRAM/OAM always accessible.
- **Serial port** with nothing connected, so test ROMs can print through it.
- **SDL2 linked statically** by default, so the executable never needs
  `SDL2.dll`.
- **PNG screenshots** written without zlib, using stored deflate blocks.
- A headless mode and `--frames` option for automated testing.
- Sound registers are stored and read back with Pan Docs read masks, and NR52
  always reports all channels off.
