# Cartridge and memory bank controllers (MBC1, MBC3, MBC5)

Files: `include/cartridge.h`, `src/cartridge.c`

## Header (0100–014F)

| Offset | Field | Use |
|--------|-------|-----|
| 0100–0103 | Entry point (usually `NOP; JP 0150`) | |
| 0104–0133 | Nintendo logo | checked |
| 0134–0143 | Title | shown in the window title and `--info` |
| 0143 | CGB flag | warns if C0 (Game Boy Color only) |
| 0146 | SGB flag | informational |
| 0147 | Cartridge type | selects the MBC; unsupported types are rejected |
| 0148 | ROM size: 32 KiB << n | sizes the ROM buffer; n > 8 is corrupt |
| 0149 | RAM size | 0, 2K, 8K, 32K, 128K or 64K |
| 014D | Header checksum over 0134–014C | checked |
| 014E–014F | Global checksum | informational |

The header checksum is computed like this:

```
x = 0
for a in 0x0134..0x014C:  x = x - rom[a] - 1
```

### Validation and errors

| Situation | Result |
|-----------|--------|
| File missing or unreadable | `could not open the ROM file` |
| Smaller than 0x150 bytes | `file is too small to be a Game Boy ROM` |
| Larger than 8 MiB | `file is larger than any Game Boy ROM` |
| Logo **and** header checksum both wrong | `not a Game Boy ROM` |
| ROM size code > 0x08 | `corrupted cartridge header` |
| Type not supported | `unsupported cartridge type`, plus the type name |
| Only the logo or only the checksum wrong | loads with a warning |

A real DMG would refuse to boot a cartridge with a bad logo or checksum. The
emulator only warns, because homebrew often gets the checksum wrong.

A file shorter than its header declares is padded with 0xFF up to the
declared size.

## ROM only (type 00, 08, 09)

The first 32 KiB are mapped directly and writes to ROM are ignored. Types 08
and 09 add 8 KiB of RAM at A000, which is always enabled.

## MBC1 (types 01, 02, 03)

There are four write-only registers, selected by the address written to:

| Address | Register | Effect |
|---------|----------|--------|
| 0000–1FFF | RAM enable | `0x0A` in the low nibble enables RAM; anything else disables it |
| 2000–3FFF | BANK1 (5 bits) | ROM bank bits 0–4. **0 is treated as 1**, checked on the 5 bits |
| 4000–5FFF | BANK2 (2 bits) | ROM bank bits 5–6, or the RAM bank |
| 6000–7FFF | MODE (1 bit) | 0 = simple, 1 = advanced |

Bank selection:

```
4000–7FFF  ROM bank = (BANK2 << 5) | BANK1
0000–3FFF  ROM bank = MODE ? (BANK2 << 5) : 0
A000–BFFF  RAM bank = MODE ? BANK2 : 0     (only when RAM is enabled)
```

All bank numbers are reduced modulo the real number of banks, because the
unused high address lines are not connected. Disabled RAM reads 0xFF.

Some well-known effects of these rules:

- Banks 0x20, 0x40 and 0x60 cannot be selected at 4000; they become 0x21,
  0x41 and 0x61.
- On a 1 MiB ROM in mode 1, 0000–3FFF shows bank 0x20.

Battery-backed RAM (type 03, 09) is loaded from `<rom>.sav` at start-up and
written back when the emulator quits.

Not supported: MBC1M multicarts.

## MBC3 (types 0F, 10, 11, 12, 13)

| Address | Effect |
|---------|--------|
| 0000–1FFF | `0x0A` enables RAM and the clock |
| 2000–3FFF | 7-bit ROM bank. 0 means 1, and there is no 0x20/0x40/0x60 quirk. MBC30 carts with more than 128 banks use 8 bits |
| 4000–5FFF | 00–03 (up to 07 on MBC30) selects a RAM bank. 08–0C maps a clock register at A000 |
| 6000–7FFF | Writing 00 then 01 **latches** the clock into the copy the CPU reads |

The real-time clock registers are:

| Register | Contents |
|----------|----------|
| 08 | seconds |
| 09 | minutes |
| 0A | hours |
| 0B | day bits 0–7 |
| 0C | bit 0 = day bit 8, bit 6 = halt, bit 7 = day overflow |

The clock runs on the host's wall clock. `cartridgeRtcUpdate()` adds the
seconds elapsed since the last update, unless the halt bit is set. After day
511 it wraps to 0 and sets the overflow bit.

The clock is saved after the RAM in `<rom>.sav`, as the 48-byte footer that
BGB and VBA-M use: 5 live and 5 latched registers as 32-bit values, then a
64-bit Unix timestamp. Time keeps passing while the emulator is closed, as it
does with a real cartridge battery.

## MBC5 (types 19–1E)

| Address | Effect |
|---------|--------|
| 0000–1FFF | `0x0A` enables RAM |
| 2000–2FFF | ROM bank bits 0–7 |
| 3000–3FFF | ROM bank bit 8 (up to 512 banks = 8 MiB) |
| 4000–5FFF | RAM bank 0–15 (up to 128 KiB). On rumble carts bit 3 drives the motor, so only bits 0–2 select the bank |

Unlike MBC1 and MBC3, **bank 0 can be mapped at 4000–7FFF**.

Not supported: MBC2, MMM01, MBC6, MBC7, HuC1, HuC3, TAMA5 and the Pocket Camera.

## Tests

`tests/test_cartridge.c` covers header parsing, every error path, file
loading, padding, MBC1 5-bit banking, 0→1, the 0x20→0x21 remap, mode 1 for
large ROMs, bank masking, and RAM enable and banking. It also covers MBC3 (7-bit
banking, RAM banks, the clock, latching, halt, day overflow, register masks,
and the `.sav` file with the clock footer) and MBC5 (9-bit banking over
8 MiB, bank 0 at 4000, 16 RAM banks, the rumble bit). `roms/mbc1_test.gb`
checks the same things from inside a running program.
