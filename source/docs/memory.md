# Memory bus

Files: `include/memory.h`, `src/memory.c`

## Memory map

| Range | Size | Contents | Handled by |
|-------|------|----------|-----------|
| 0000–3FFF | 16 KiB | ROM bank 0 (MBC1 mode 1 can remap it) | `cartridgeRead` |
| 4000–7FFF | 16 KiB | Switchable ROM bank | `cartridgeRead` |
| 0000–7FFF (writes) | | MBC registers | `cartridgeWrite` |
| 8000–9FFF | 8 KiB | VRAM: tiles 8000–97FF, maps 9800/9C00 | `vram[]` |
| A000–BFFF | 8 KiB | External cartridge RAM | `cartridgeRead/Write` |
| C000–DFFF | 8 KiB | Work RAM | `wram[]` |
| E000–FDFF | | Echo of C000–DDFF | `wram[]` |
| FE00–FE9F | 160 B | OAM (40 sprites × 4 bytes) | `oam[]` |
| FEA0–FEFF | | Unusable: reads 0xFF, writes ignored | |
| FF00–FF7F | | I/O registers | see below |
| FF80–FFFE | 127 B | High RAM | `hram[]` |
| FFFF | | IE | `interrupt.enable` |

## I/O registers

| Address | Register | Owner |
|---------|----------|-------|
| FF00 | P1 (joypad) | `input.c` |
| FF01–FF02 | SB, SC (serial) | `serial.c` |
| FF04–FF07 | DIV, TIMA, TMA, TAC | `timer.c` |
| FF0F | IF (bits 5–7 read as 1) | `interrupts.c` |
| FF10–FF3F | Sound (**stored only, not emulated**) | `memory.c` |
| FF40–FF4B | LCDC, STAT, SCY, SCX, LY, LYC, DMA, BGP, OBP0, OBP1, WY, WX | `ppu.c` |
| FF46 | Also starts the OAM DMA copy | `memory.c` |
| FF50 | Disables the boot ROM overlay | `memory.c` |
| other | Unmapped or CGB-only: reads 0xFF | |

## Sound registers (no sound)

The emulator does not generate sound. Games still write to NR10–NR52 and the
wave RAM, and some read them back. For those registers, `memory.c`:

- stores every write in `io[]`;
- ORs reads with the Pan Docs read masks, because write-only bits read as 1;
- makes NR52 (FF26) return only the power bit plus 0x70, so all channels
  always read as inactive.

Nothing else in the emulator reads these values, so missing sound cannot
affect the CPU, PPU, timer or input.

## OAM DMA

Writing XX to FF46 copies XX00–XX9F into OAM. For simplicity the copy happens
immediately. On hardware it takes 160 M-cycles, during which the CPU can only
access HRAM. Games copy a routine to HRAM and wait there anyway, so the
difference is not visible to them.

## Helpers

`readShort` and `writeShort` are little-endian. `writeShortToStack` and
`readShortFromStack` implement PUSH and POP (Cinoop naming).
