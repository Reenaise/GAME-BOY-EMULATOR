#!/usr/bin/env python3
"""
Generates the homebrew test ROMs in roms/ (no external assembler needed).

  demo.gb       Interactive demo: font from ROM bank 2, sprite from bank 3
                (MBC1 banking), window, sprite movement with the d-pad,
                V-Blank + timer interrupts, OAM DMA from HRAM.
  mbc1_test.gb  Self-checking MBC1 test (ROM banks, RAM enable, RAM banks).
                Prints "MBC1 PASS"/"MBC1 FAIL" over the serial port and
                writes 0x01 (pass) / 0xFF (fail) to 0xC000.

These ROMs are original work written for this project and are free to
redistribute with it.

Usage:  python tools/make_test_roms.py      (writes to roms/)
"""

import os
import sys

LOGO = bytes([
    0xCE, 0xED, 0x66, 0x66, 0xCC, 0x0D, 0x00, 0x0B, 0x03, 0x73, 0x00, 0x83,
    0x00, 0x0C, 0x00, 0x0D, 0x00, 0x08, 0x11, 0x1F, 0x88, 0x89, 0x00, 0x0E,
    0xDC, 0xCC, 0x6E, 0xE6, 0xDD, 0xDD, 0xD9, 0x99, 0xBB, 0xBB, 0x67, 0x63,
    0x6E, 0x0E, 0xEC, 0xCC, 0xDD, 0xDC, 0x99, 0x9F, 0xBB, 0xB9, 0x33, 0x3E,
])

REG = {"b": 0, "c": 1, "d": 2, "e": 3, "h": 4, "l": 5, "(hl)": 6, "a": 7}
COND = {None: 0, "nz": 0, "z": 1, "nc": 2, "c": 3}


class Asm:
    """Just enough of an assembler: raw bytes plus label fix-ups."""

    def __init__(self, banks):
        self.rom = bytearray(banks * 0x4000)
        self.banks = banks
        self.pc = 0
        self.labels = {}
        self.fixups = []

    # -- emission -----------------------------------------------------------
    def org(self, address):
        self.pc = address

    def label(self, name):
        assert name not in self.labels, name
        self.labels[name] = self.pc

    def db(self, *values):
        for v in values:
            if isinstance(v, (bytes, bytearray)):
                for x in v:
                    self.db(x)
                continue
            if isinstance(v, str):
                self.db(v.encode("ascii"))
                continue
            self.rom[self.pc] = v & 0xFF
            self.pc += 1

    def _abs(self, target):
        if isinstance(target, str):
            self.fixups.append((self.pc, target, "abs"))
            self.db(0, 0)
        else:
            self.db(target & 0xFF, target >> 8)

    def _rel(self, target):
        self.fixups.append((self.pc, target, "rel"))
        self.db(0)

    # -- instructions ------------------------------------------------------
    def jp(self, target, cc=None):
        self.db(0xC3 if cc is None else 0xC2 + COND[cc] * 8)
        self._abs(target)

    def jr(self, target, cc=None):
        self.db(0x18 if cc is None else 0x20 + COND[cc] * 8)
        self._rel(target)

    def call(self, target):
        self.db(0xCD)
        self._abs(target)

    def ld16(self, rr, value):
        self.db({"bc": 0x01, "de": 0x11, "hl": 0x21, "sp": 0x31}[rr])
        self._abs(value)

    def ld(self, r, n):        # LD r, n
        self.db(0x06 + REG[r] * 8, n)

    def ldr(self, dst, src):   # LD r, r'
        self.db(0x40 + REG[dst] * 8 + REG[src])

    def st_a(self, address):   # LD (nn), A
        self.db(0xEA)
        self._abs(address)

    def ld_a(self, address):   # LD A, (nn)
        self.db(0xFA)
        self._abs(address)

    def ldh_w(self, reg):      # LDH (FF00+n), A
        self.db(0xE0, reg & 0xFF)

    def ldh_r(self, reg):      # LDH A, (FF00+n)
        self.db(0xF0, reg & 0xFF)

    def cp(self, n):  self.db(0xFE, n)
    def and_(self, n): self.db(0xE6, n)
    def xor(self, n): self.db(0xEE, n)
    def add(self, n): self.db(0xC6, n)
    def sub(self, n): self.db(0xD6, n)

    def bit(self, b, r):
        self.db(0xCB, 0x40 + b * 8 + REG[r])

    # -- finishing -----------------------------------------------------------
    def finish(self, title, cart_type, rom_code, ram_code):
        for at, target, kind in self.fixups:
            address = self.labels[target]
            if kind == "abs":
                self.rom[at] = address & 0xFF
                self.rom[at + 1] = address >> 8
            else:
                offset = address - (at + 1)
                assert -128 <= offset <= 127, (target, offset)
                self.rom[at] = offset & 0xFF

        rom = self.rom
        rom[0x104:0x134] = LOGO
        t = title.encode("ascii")[:15]
        rom[0x134:0x144] = t + bytes(16 - len(t))
        rom[0x147] = cart_type
        rom[0x148] = rom_code
        rom[0x149] = ram_code
        rom[0x14A] = 0x01
        checksum = 0
        for i in range(0x134, 0x14D):
            checksum = (checksum - rom[i] - 1) & 0xFF
        rom[0x14D] = checksum
        total = (sum(rom) - rom[0x14E] - rom[0x14F]) & 0xFFFF
        rom[0x14E] = total >> 8
        rom[0x14F] = total & 0xFF
        return bytes(rom)


def common_start(a, entry="start"):
    """Entry point at 0x100 and the standard header gap."""
    a.org(0x100)
    a.db(0x00)       # NOP
    a.jp(entry)      # JP start
    a.org(0x150)


def emit_memcpy(a):
    # memcpy: HL = source, DE = destination, BC = count
    a.label("memcpy")
    a.ldr("a", "b"); a.db(0xB1)          # LD A,B ; OR C
    a.db(0xC8)                           # RET Z
    a.label("memcpy_loop")
    a.db(0x2A, 0x12, 0x13, 0x0B)         # LD A,(HL+) ; LD (DE),A ; INC DE ; DEC BC
    a.ldr("a", "b"); a.db(0xB1)          # LD A,B ; OR C
    a.jr("memcpy_loop", "nz")
    a.db(0xC9)                           # RET

    # memset: HL = destination, BC = count (>0), A = value
    a.label("memset")
    a.ldr("d", "a")
    a.label("memset_loop")
    a.ldr("a", "d"); a.db(0x22, 0x0B)    # LD A,D ; LD (HL+),A ; DEC BC
    a.ldr("a", "b"); a.db(0xB1)
    a.jr("memset_loop", "nz")
    a.db(0xC9)


# ---------------------------------------------------------------------------
# Font: 5x7 glyphs drawn in colour 3, tile index = ASCII code
# ---------------------------------------------------------------------------
GLYPHS = {
    "A": ["01110", "10001", "10001", "11111", "10001", "10001", "10001"],
    "B": ["11110", "10001", "10001", "11110", "10001", "10001", "11110"],
    "C": ["01110", "10001", "10000", "10000", "10000", "10001", "01110"],
    "D": ["11110", "10001", "10001", "10001", "10001", "10001", "11110"],
    "E": ["11111", "10000", "10000", "11110", "10000", "10000", "11111"],
    "F": ["11111", "10000", "10000", "11110", "10000", "10000", "10000"],
    "G": ["01110", "10001", "10000", "10111", "10001", "10001", "01111"],
    "H": ["10001", "10001", "10001", "11111", "10001", "10001", "10001"],
    "I": ["01110", "00100", "00100", "00100", "00100", "00100", "01110"],
    "J": ["00111", "00010", "00010", "00010", "00010", "10010", "01100"],
    "K": ["10001", "10010", "10100", "11000", "10100", "10010", "10001"],
    "L": ["10000", "10000", "10000", "10000", "10000", "10000", "11111"],
    "M": ["10001", "11011", "10101", "10101", "10001", "10001", "10001"],
    "N": ["10001", "10001", "11001", "10101", "10011", "10001", "10001"],
    "O": ["01110", "10001", "10001", "10001", "10001", "10001", "01110"],
    "P": ["11110", "10001", "10001", "11110", "10000", "10000", "10000"],
    "Q": ["01110", "10001", "10001", "10001", "10101", "10010", "01101"],
    "R": ["11110", "10001", "10001", "11110", "10100", "10010", "10001"],
    "S": ["01111", "10000", "10000", "01110", "00001", "00001", "11110"],
    "T": ["11111", "00100", "00100", "00100", "00100", "00100", "00100"],
    "U": ["10001", "10001", "10001", "10001", "10001", "10001", "01110"],
    "V": ["10001", "10001", "10001", "10001", "10001", "01010", "00100"],
    "W": ["10001", "10001", "10001", "10101", "10101", "10101", "01010"],
    "X": ["10001", "10001", "01010", "00100", "01010", "10001", "10001"],
    "Y": ["10001", "10001", "01010", "00100", "00100", "00100", "00100"],
    "Z": ["11111", "00001", "00010", "00100", "01000", "10000", "11111"],
    "0": ["01110", "10001", "10011", "10101", "11001", "10001", "01110"],
    "1": ["00100", "01100", "00100", "00100", "00100", "00100", "01110"],
    "2": ["01110", "10001", "00001", "00010", "00100", "01000", "11111"],
    "3": ["11111", "00010", "00100", "00010", "00001", "10001", "01110"],
    "4": ["00010", "00110", "01010", "10010", "11111", "00010", "00010"],
    "5": ["11111", "10000", "11110", "00001", "00001", "10001", "01110"],
    "6": ["00110", "01000", "10000", "11110", "10001", "10001", "01110"],
    "7": ["11111", "00001", "00010", "00100", "01000", "01000", "01000"],
    "8": ["01110", "10001", "10001", "01110", "10001", "10001", "01110"],
    "9": ["01110", "10001", "10001", "01111", "00001", "00010", "01100"],
    ":": ["00000", "01100", "01100", "00000", "01100", "01100", "00000"],
    "!": ["00100", "00100", "00100", "00100", "00100", "00000", "00100"],
    "-": ["00000", "00000", "00000", "11111", "00000", "00000", "00000"],
    ".": ["00000", "00000", "00000", "00000", "00000", "01100", "01100"],
    "/": ["00001", "00010", "00010", "00100", "01000", "01000", "10000"],
}


def font_tiles():
    data = bytearray(128 * 16)
    for ch, rows in GLYPHS.items():
        base = ord(ch) * 16
        for y, row in enumerate(rows):
            bits = int(row, 2) << 2          # 5 pixels, centred in 8
            data[base + (y + 1) * 2] = bits  # low plane
            data[base + (y + 1) * 2 + 1] = bits  # high plane -> colour 3
    # tile 0x01: light dotted pattern for the window background
    data[0x10:0x20] = bytes([0xAA, 0x00, 0x55, 0x00] * 4)
    return bytes(data)


def sprite_tile():
    face = [0x3C, 0x7E, 0xFF, 0xFF, 0xFF, 0xFF, 0x7E, 0x3C]
    features = [0x00, 0x00, 0x24, 0x00, 0x42, 0x3C, 0x00, 0x00]
    out = bytearray()
    for lo, hi in zip(face, features):
        out += bytes([lo, hi])
    return bytes(out)


# ---------------------------------------------------------------------------
# demo.gb
# ---------------------------------------------------------------------------
SHADOW_OAM = 0xC000
FRAMES = 0xC100
TICKS = 0xC101
DIGIT = 0xC102
SCROLL = 0xC103
PREV = 0xC104
VBLANK_FLAG = 0xC105
MBC_RESULT = 0xC106


def build_demo():
    a = Asm(banks=4)

    # Interrupt vectors
    a.org(0x40); a.jp("vblank")
    a.org(0x50); a.jp("timer")
    common_start(a)

    a.label("start")
    a.db(0xF3)                              # DI
    a.ld16("sp", 0xFFFE)
    a.label("wait_vblank")                  # LCD may only be turned off in V-Blank
    a.ldh_r(0x44); a.cp(144)
    a.jr("wait_vblank", "c")
    a.db(0xAF); a.ldh_w(0x40)               # LCD off

    # Font from ROM bank 2 -> tiles 0-127 (8000-87FF)
    a.ld("a", 2); a.st_a(0x2000)
    a.ld16("hl", 0x4000); a.ld16("de", 0x8000); a.ld16("bc", 0x0800)
    a.call("memcpy")
    # Sprite from ROM bank 3 -> tile 128 (8800)
    a.ld("a", 3); a.st_a(0x2000)
    a.ld16("hl", 0x4000); a.ld16("de", 0x8800); a.ld16("bc", 0x0010)
    a.call("memcpy")

    # Clear both tile maps with spaces, window map with pattern tile 1
    a.ld16("hl", 0x9800); a.ld16("bc", 0x0400); a.ld("a", 0x20)
    a.call("memset")
    a.ld16("hl", 0x9C00); a.ld16("bc", 0x0400); a.ld("a", 0x01)
    a.call("memset")
    a.ld16("hl", "strings"); a.call("print")

    # MBC1 self-check: the last byte of every bank holds its number
    for bank in (2, 3, 1):
        a.ld("a", bank); a.st_a(0x2000)
        a.ld_a(0x7FFF); a.cp(bank)
        a.jr("mbc_bad", "nz")
    a.ld16("hl", "str_ok"); a.ld("a", 1)
    a.jr("mbc_print")
    a.label("mbc_bad")
    a.ld16("hl", "str_bad"); a.ld("a", 0xBB)
    a.label("mbc_print")
    a.st_a(MBC_RESULT)
    a.call("print")

    # Shadow OAM: one sprite in the middle of the screen
    a.ld16("hl", SHADOW_OAM); a.ld16("bc", 0x00A0); a.db(0xAF)
    a.call("memset")
    a.ld16("hl", SHADOW_OAM)
    a.db(0x36, 80, 0x23, 0x36, 84, 0x23, 0x36, 128, 0x23, 0x36, 0x00)

    # DMA routine -> HRAM
    a.ld16("hl", "dma_routine"); a.ld16("de", 0xFF80); a.ld16("bc", 10)
    a.call("memcpy")

    # Variables
    a.ld16("hl", FRAMES); a.ld16("bc", 6); a.db(0xAF)
    a.call("memset")
    a.ld("a", ord("0")); a.st_a(DIGIT)

    # Palettes, window position
    a.ld("a", 0xE4); a.ldh_w(0x47); a.ldh_w(0x48); a.ldh_w(0x49)
    a.ld("a", 128); a.ldh_w(0x4A)           # WY
    a.ld("a", 7); a.ldh_w(0x4B)             # WX

    # Timer: 4096 Hz, TMA = 0 -> 16 overflows per second
    a.db(0xAF); a.ldh_w(0x06); a.ldh_w(0x05)
    a.ld("a", 0x04); a.ldh_w(0x07)

    # Interrupts: V-Blank + timer
    a.ld("a", 0x05); a.ldh_w(0xFF)
    a.db(0xAF); a.ldh_w(0x0F)

    # LCD on: window map 9C00, window on, tiles 8000, sprites on, BG on
    a.ld("a", 0xF3); a.ldh_w(0x40)
    a.db(0xFB)                              # EI

    a.label("main")
    a.db(0x76, 0x00)                        # HALT ; NOP
    a.ld_a(VBLANK_FLAG); a.db(0xA7)         # AND A
    a.jr("main", "z")
    a.db(0xAF); a.st_a(VBLANK_FLAG)

    a.call("read_joypad")
    a.ldr("b", "a")
    # D-pad moves the sprite
    for bit, address, op, name in ((0, 0xC001, 0x34, "r"), (1, 0xC001, 0x35, "l"),
                                   (2, 0xC000, 0x35, "u"), (3, 0xC000, 0x34, "d")):
        a.bit(bit, "b")
        a.jr("skip_" + name, "z")
        a.ld16("hl", address)
        a.db(op)                            # INC/DEC (HL)
        a.label("skip_" + name)
    # Newly pressed buttons -> C
    a.ld_a(PREV); a.db(0x2F, 0xA0)          # CPL ; AND B
    a.ldr("c", "a")
    a.ldr("a", "b"); a.st_a(PREV)
    # A: invert the background palette
    a.bit(4, "c")
    a.jr("skip_a", "z")
    a.ldh_r(0x47); a.db(0x2F); a.ldh_w(0x47)
    a.label("skip_a")
    # B: flip the sprite horizontally
    a.bit(5, "c")
    a.jr("skip_b", "z")
    a.ld_a(0xC003); a.xor(0x20); a.st_a(0xC003)
    a.label("skip_b")
    # Start: toggle background scrolling
    a.bit(7, "c")
    a.jr("skip_start", "z")
    a.ld_a(SCROLL); a.xor(1); a.st_a(SCROLL)
    a.label("skip_start")
    a.jr("main")

    # --- V-Blank handler ---
    a.label("vblank")
    a.db(0xF5, 0xE5)                        # PUSH AF ; PUSH HL
    a.call(0xFF80)                          # OAM DMA
    a.ld_a(DIGIT); a.st_a(0x9C0B)           # seconds digit in the window
    a.ld_a(SCROLL); a.db(0xA7)
    a.jr("no_scroll", "z")
    a.ldh_r(0x43); a.db(0x3C); a.ldh_w(0x43)   # SCX++
    a.label("no_scroll")
    a.ld("a", 1); a.st_a(VBLANK_FLAG)
    a.ld16("hl", FRAMES); a.db(0x34)        # INC (HL)
    a.db(0xE1, 0xF1, 0xD9)                  # POP HL ; POP AF ; RETI

    # --- Timer handler: 16 overflows = 1 second ---
    a.label("timer")
    a.db(0xF5)
    a.ld_a(TICKS); a.db(0x3C); a.st_a(TICKS)
    a.cp(16)
    a.jr("timer_done", "nz")
    a.db(0xAF); a.st_a(TICKS)
    a.ld_a(DIGIT); a.db(0x3C); a.cp(ord("9") + 1)
    a.jr("digit_ok", "nz")
    a.ld("a", ord("0"))
    a.label("digit_ok")
    a.st_a(DIGIT)
    a.label("timer_done")
    a.db(0xF1, 0xD9)

    # --- read_joypad: returns A = Start Select B A Down Up Left Right (1 = held)
    a.label("read_joypad")
    a.ld("a", 0x20); a.ldh_w(0x00)
    a.ldh_r(0x00); a.ldh_r(0x00)
    a.db(0x2F); a.and_(0x0F); a.ldr("b", "a")
    a.ld("a", 0x10); a.ldh_w(0x00)
    a.ldh_r(0x00); a.ldh_r(0x00)
    a.db(0x2F); a.and_(0x0F); a.db(0xCB, 0x37, 0xB0)   # SWAP A ; OR B
    a.ldr("b", "a")
    a.ld("a", 0x30); a.ldh_w(0x00)
    a.ldr("a", "b")
    a.db(0xC9)

    # --- print: HL -> list of [addr_hi, addr_lo, text..., 0], ends with 0
    a.label("print")
    a.db(0x2A, 0xA7, 0xC8)                  # LD A,(HL+) ; AND A ; RET Z
    a.ldr("d", "a"); a.db(0x2A); a.ldr("e", "a")
    a.label("print_char")
    a.db(0x2A, 0xA7)
    a.jr("print", "z")
    a.db(0x12, 0x13)                        # LD (DE),A ; INC DE
    a.jr("print_char")

    emit_memcpy(a)

    a.label("dma_routine")
    a.db(0x3E, 0xC0, 0xE0, 0x46, 0x3E, 0x28, 0x3D, 0x20, 0xFD, 0xC9)

    def text(row, col, s, base=0x9800):
        address = base + row * 32 + col
        a.db(address >> 8, address & 0xFF, s, 0)

    a.label("strings")
    text(1, 3, "GAME BOY DMG")
    text(2, 4, "EMULATOR DEMO")
    text(5, 1, "ARROWS: MOVE")
    text(6, 1, "A: INVERT")
    text(7, 1, "B: FLIP SPRITE")
    text(8, 1, "START: SCROLL")
    text(11, 1, "MBC1 BANKS:")
    text(0, 1, "SECONDS:", base=0x9C00)
    a.db(0)
    a.label("str_ok")
    text(11, 13, "OK")
    a.db(0)
    a.label("str_bad")
    text(11, 13, "BAD")
    a.db(0)

    assert a.pc < 0x4000, "bank 0 overflow"

    # Bank 1: marker only. Bank 2: font. Bank 3: sprite.
    a.org(0x8000); a.db(font_tiles())
    a.org(0xC000); a.db(sprite_tile())
    for bank in range(1, 4):
        a.rom[bank * 0x4000 + 0x3FFF] = bank

    return a.finish("DEMO", 0x01, 0x01, 0x00)   # MBC1, 64 KiB, no RAM


# ---------------------------------------------------------------------------
# mbc1_test.gb
# ---------------------------------------------------------------------------
def build_mbc1_test():
    a = Asm(banks=8)
    common_start(a)

    a.label("start")
    a.db(0xF3)
    a.ld16("sp", 0xFFFE)

    # 1. every switchable ROM bank shows its own marker at 0x4000
    a.ld("e", 1)
    a.label("rom_loop")
    a.ldr("a", "e"); a.st_a(0x2000)
    a.ld_a(0x4000); a.db(0xBB)              # CP E
    a.jr("fail", "nz")
    a.db(0x1C); a.ldr("a", "e"); a.cp(8)    # INC E
    a.jr("rom_loop", "nz")

    # 2. writing bank 0 selects bank 1
    a.db(0xAF); a.st_a(0x2000)
    a.ld_a(0x4000); a.cp(1)
    a.jr("fail", "nz")

    # 3. RAM disabled reads 0xFF
    a.ld_a(0xA000); a.cp(0xFF)
    a.jr("fail", "nz")

    # 4. enable RAM, mode 1, write a marker into each of the 4 RAM banks
    a.ld("a", 0x0A); a.st_a(0x0000)
    a.ld("a", 1); a.st_a(0x6000)
    a.ld("e", 0)
    a.label("ram_write")
    a.ldr("a", "e"); a.st_a(0x4000)
    a.ldr("a", "e"); a.add(0x50); a.st_a(0xA000)
    a.db(0x1C); a.ldr("a", "e"); a.cp(4)
    a.jr("ram_write", "nz")

    # 5. read them back
    a.ld("e", 0)
    a.label("ram_read")
    a.ldr("a", "e"); a.st_a(0x4000)
    a.ld_a(0xA000); a.sub(0x50); a.db(0xBB)
    a.jr("fail", "nz")
    a.db(0x1C); a.ldr("a", "e"); a.cp(4)
    a.jr("ram_read", "nz")

    # 6. disable RAM again
    a.db(0xAF); a.st_a(0x0000)
    a.ld_a(0xA000); a.cp(0xFF)
    a.jr("fail", "nz")

    a.ld16("hl", "msg_pass"); a.call("serial_print")
    a.ld("a", 0x01); a.st_a(0xC000)
    a.jr("done")
    a.label("fail")
    a.ld16("hl", "msg_fail"); a.call("serial_print")
    a.ld("a", 0xFF); a.st_a(0xC000)
    a.label("done")
    a.jr("done")

    a.label("serial_print")
    a.db(0x2A, 0xA7, 0xC8)                  # LD A,(HL+) ; AND A ; RET Z
    a.ldh_w(0x01)
    a.ld("a", 0x81); a.ldh_w(0x02)
    a.label("serial_wait")
    a.ldh_r(0x02); a.bit(7, "a")
    a.jr("serial_wait", "nz")
    a.jr("serial_print")

    a.label("msg_pass"); a.db("MBC1 PASS\n", 0)
    a.label("msg_fail"); a.db("MBC1 FAIL\n", 0)

    for bank in range(1, 8):
        a.rom[bank * 0x4000] = bank

    return a.finish("MBC1TEST", 0x02, 0x02, 0x03)  # MBC1+RAM, 128 KiB, 32 KiB RAM


def main():
    root = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "roms")
    os.makedirs(root, exist_ok=True)
    for name, builder in (("demo.gb", build_demo), ("mbc1_test.gb", build_mbc1_test)):
        path = os.path.join(root, name)
        with open(path, "wb") as f:
            f.write(builder())
        print("wrote", os.path.normpath(path))
    return 0


if __name__ == "__main__":
    sys.exit(main())
