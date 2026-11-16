"""Mappers (runtime/cart.c) on tiny hand-assembled ROMs:
each switches a bank, reads a marker byte from it and stores it at $00, which
nes_headless --peek reports. The other mappers are covered by test ROMs
(tests/test_roms.py) and the recompiled games."""
import os
import subprocess
import tempfile
import unittest

from tests.test_cpu import BUILD

RUNNER = os.path.join(BUILD, "nes_headless.exe" if os.name == "nt" else "nes_headless")

WAIT_VBLANK = bytes([0x2C, 0x02, 0x20, 0x10, 0xFB]) * 2  # BIT $2002 / BPL: PPU warm-up
CHR_READ = bytes([0xA9, 0x00, 0x8D, 0x06, 0x20, 0x8D, 0x06, 0x20,  # $2006 = $0000
                  0xAD, 0x07, 0x20, 0xAD, 0x07, 0x20])               # LDA $2007 (buffered) x2
END = bytes([0x85, 0x00, 0x18, 0x90, 0xFE])                          # STA $00, then loop


def ppu_read(addr):  # LDA from PPU address `addr` ($2007, buffered: read twice)
    return bytes([0xA9, addr >> 8, 0x8D, 0x06, 0x20, 0xA9, addr & 0xFF, 0x8D, 0x06, 0x20,
                  0xAD, 0x07, 0x20, 0xAD, 0x07, 0x20])


def ram_loop(start):  # INC $10 / BNE / INC $11 / JMP: 8 cycles a turn, no slow bus access
    return bytes([0xE6, 0x10, 0xD0, 0xFC, 0xE6, 0x11, 0x4C, start & 0xFF, start >> 8])


def lda_sta(value, addr):
    return bytes([0xA9, value, 0x8D, addr & 0xFF, addr >> 8])


def lda_abs(addr):
    return bytes([0xAD, addr & 0xFF, addr >> 8])


def ines(mapper, prg, chr_=b"", submapper=None, ram_sizes=None):
    hdr = bytearray(b"NES\x1a" + bytes([len(prg) // 0x4000, len(chr_) // 0x2000,
                                        (mapper & 0x0F) << 4, mapper & 0xF0]) + bytes(8))
    if submapper is not None or ram_sizes is not None:  # NES 2.0
        hdr[7] |= 0x08
        hdr[8] = (submapper or 0) << 4
        hdr[10] = ram_sizes or 0  # PRG RAM / NVRAM size shifts
    return bytes(hdr) + bytes(prg) + bytes(chr_)


def put(prg, offset, data):
    prg[offset:offset + len(data)] = data


def vectors(prg, end, reset, irq=0xFFF9):  # NMI (and IRQ) at an RTI just before the vectors
    put(prg, end - 7, bytes([0x40]))
    rti = 0xFFF9
    put(prg, end - 6, bytes([rti & 0xFF, rti >> 8, reset & 0xFF, reset >> 8, irq & 0xFF, irq >> 8]))


def peek(rom, addr=0):
    with tempfile.TemporaryDirectory() as tmp:
        path = os.path.join(tmp, "t.nes")
        with open(path, "wb") as f:
            f.write(rom)
        r = subprocess.run([RUNNER, path, "--frames", "10", "--peek", "%X" % addr],
                           capture_output=True, text=True, timeout=60)
    for line in r.stdout.splitlines():
        if line.startswith("PEEK"):
            return int(line.split("=")[1], 16)
    raise AssertionError(r.stdout + r.stderr)


@unittest.skipUnless(os.path.exists(RUNNER), "build the C runtime first (see tests/test_cpu.py)")
class TestMappers(unittest.TestCase):
    def test_71_camerica(self):  # 16 KB at $8000 from writes to $C000-$FFFF, last bank fixed
        prg = bytearray(0x10000)
        for b in range(4):
            prg[b * 0x4000 + 0x100] = 0x10 + b
        put(prg, 0xC200, lda_sta(0x02, 0xC000) + lda_abs(0x8100) + END)
        vectors(prg, 0x10000, 0xC200)
        self.assertEqual(peek(ines(71, prg)), 0x12)

    def test_79_nina(self):  # 32 KB from writes to $4100 (A8 set); $4000 is not the register
        prg = bytearray(0x10000)
        code = (lda_sta(0x08, 0x4000) + lda_abs(0x8100) + bytes([0x85, 0x01]) +
                lda_sta(0x08, 0x4100) + lda_abs(0x8100) + END)
        for b in range(2):  # the same code in both banks: it runs on after the switch
            prg[b * 0x8000 + 0x100] = 0xA0 + b
            put(prg, b * 0x8000 + 0x200, code)
            vectors(prg, (b + 1) * 0x8000, 0x8200)
        rom = ines(79, prg)
        self.assertEqual(peek(rom, 1), 0xA0)
        self.assertEqual(peek(rom, 0), 0xA1)

    def test_87_jaleco(self):  # 8 KB CHR from writes to $6000-$7FFF, bits 0 and 1 swapped
        prg, chr_ = bytearray(0x8000), bytearray(0x8000)
        for b in range(4):
            chr_[b * 0x2000] = 0xC0 + b
        put(prg, 0x200, WAIT_VBLANK + lda_sta(0x01, 0x6000) + CHR_READ + END)
        vectors(prg, 0x8000, 0x8200)
        self.assertEqual(peek(ines(87, prg, chr_)), 0xC2)

    def test_185_chr_protection(self):  # submapper 4: CHR readable only while latch & 3 == 0
        for latch, expect in ((0x00, 0x5A), (0x01, 0xFF)):
            prg, chr_ = bytearray(0x8000), bytearray(0x2000)
            chr_[0] = 0x5A
            put(prg, 0x200, WAIT_VBLANK + lda_sta(latch, 0x8000) + CHR_READ + END)
            vectors(prg, 0x8000, 0x8200)
            with self.subTest(latch=latch):
                self.assertEqual(peek(ines(185, prg, chr_, submapper=4)), expect)

    def test_206_namco108(self):  # MMC3 R6 at $8000; the PRG mode bit (0x40) does not exist
        prg = bytearray(0x20000)
        for b in range(16):
            prg[b * 0x2000 + 0x100] = 0x60 + b
        put(prg, 0x1E200, lda_sta(0x46, 0x8000) + lda_sta(0x02, 0x8001) + lda_abs(0x8100) + END)
        vectors(prg, 0x20000, 0xE200)
        self.assertEqual(peek(ines(206, prg)), 0x62)  # 0x6E if mode 1 were honoured

    def test_23_vrc4_prg_swap(self):  # VRC4e wiring (A2, A3): $9008 is the swap-mode register
        prg = bytearray(0x20000)
        for b in range(16):
            prg[b * 0x2000 + 0x100] = 0x60 + b
        code = (lda_sta(0x03, 0x8000) + lda_sta(0x02, 0x9008) +
                lda_abs(0x8100) + bytes([0x85, 0x01]) + lda_abs(0xC100) + END)
        put(prg, 0x1E200, code)
        vectors(prg, 0x20000, 0xE200)
        for sub in (2, None):  # NES 2.0 VRC4e, and an old header (both wirings decoded)
            with self.subTest(submapper=sub):
                rom = ines(23, prg, submapper=sub)
                self.assertEqual(peek(rom, 0), 0x63)  # PRG 0 now at $C000
                self.assertEqual(peek(rom, 1), 0x6E)  # second-last bank at $8000

    def test_22_vrc2a_chr(self):  # VRC2a ignores CHR bit 0: register 4 = 1 KB bank 2
        prg, chr_ = bytearray(0x8000), bytearray(0x8000)
        for b in range(32):
            chr_[b * 0x400] = 0x80 + b
        put(prg, 0x6200, WAIT_VBLANK + lda_sta(0x04, 0xB000) + CHR_READ + END)
        vectors(prg, 0x8000, 0xE200)
        self.assertEqual(peek(ines(22, prg, chr_)), 0x82)

    def test_25_vrc4_irq(self):
        """VRC4b wiring (A1, A0): latch $F000/$F002, control $F001, ack $F003.
        IRQs counted from one vblank to the next (29,780.7 CPU cycles)."""
        handler = bytes([0xE6, 0x00, 0x8D, 0x03, 0xF0, 0x40])  # INC $00, STA $F003 (ack), RTI
        bpl_wait = bytes([0x2C, 0x02, 0x20, 0x10, 0xFB])
        # scanline mode, 20 lines (latch $EC): every 2,273.3 cycles = 13; cycle mode, 256 cycles = 116
        for latch, ctrl, expect in ((0xEC, 0x03, 13), (0x00, 0x07, 116)):
            prg = bytearray(0x8000)
            code = (bytes([0x78]) + lda_sta(0x40, 0x4017) +  # APU frame IRQ off: only the VRC's
                    WAIT_VBLANK + lda_sta(latch & 0x0F, 0xF000) + lda_sta(latch >> 4, 0xF002) +
                    bpl_wait + lda_sta(0x00, 0x0000)[:2] + bytes([0x85, 0x00]) +  # LDA #0, STA $00
                    lda_sta(ctrl, 0xF001) + bytes([0x58]) +                      # control, CLI
                    bpl_wait + bytes([0x78, 0xA5, 0x00]) + END)                    # SEI, LDA $00
            put(prg, 0x6200, code)
            put(prg, 0x6400, handler)
            vectors(prg, 0x8000, 0xE200, irq=0xE400)
            with self.subTest(mode="cycle" if ctrl & 4 else "scanline"):
                self.assertEqual(peek(ines(25, prg, submapper=1)), expect)

    def test_25_vrc4_irq_on_time(self):
        """The IRQ must arrive on time with no slow bus access to catch the
        cart up (the bus plans for it, nes.c plan_next_event). A RAM loop of 8
        cycles counts until the first IRQ: 20 lines = 2,274 cycles = ~284."""
        prg = bytearray(0x8000)
        handler = bytes([0xA5, 0x10, 0x85, 0x00, 0xA5, 0x11, 0x85, 0x01,  # $00/$01 = loop count
                         0xA9, 0x00, 0x8D, 0x01, 0xF0, 0x40])              # IRQ off, RTI
        code = (bytes([0x78]) + lda_sta(0x40, 0x4017) + WAIT_VBLANK + lda_sta(0x0C, 0xF000) +
                lda_sta(0x0E, 0xF002) + lda_sta(0x03, 0xF001) + bytes([0x58]))
        loop = 0xE200 + len(code)  # INC $10 / BNE loop / INC $11 / JMP loop
        code += bytes([0xE6, 0x10, 0xD0, 0xFC, 0xE6, 0x11, 0x4C, loop & 0xFF, loop >> 8])
        put(prg, 0x6200, code)
        put(prg, 0x6400, handler)
        vectors(prg, 0x8000, 0xE200, irq=0xE400)
        rom = ines(25, prg, submapper=1)
        self.assertAlmostEqual(peek(rom, 0) + 256 * peek(rom, 1), 284, delta=1)

    # ---- MMC5 (5): power-on mode 3 with the last bank at $E000 ----
    def test_5_mmc5_two_ram_chips(self):  # 8 KB RAM + 8 KB NVRAM: $5113 bit 2 picks the chip
        code = (lda_sta(0x00, 0x5113) + lda_sta(0x11, 0x6000) + lda_sta(0x04, 0x5113) + lda_sta(0x44, 0x6000) +
                lda_sta(0x01, 0x5113) + lda_abs(0x6000) + bytes([0x85, 0x01]) +  # bank 1 = chip 0 again
                lda_sta(0x00, 0x5113) + lda_abs(0x6000) + END)
        rom = self.mmc5(code, ram_sizes=0x77)
        self.assertEqual(peek(rom, 0), 0x11)  # 0x44 if banks 0 and 4 shared memory
        self.assertEqual(peek(rom, 1), 0x11)

    @staticmethod
    def mmc5(code, chr_=b"", ram_sizes=None):
        prg = bytearray(0x20000)
        for b in range(16):
            prg[b * 0x2000 + 0x100] = 0x60 + b
        put(prg, 0x1E200, code)
        put(prg, 0x1E400, bytes([0xA5, 0x10, 0x85, 0x00, 0xA5, 0x11, 0x85, 0x01,  # IRQ: $00/$01 = loop
                                 0xAD, 0x04, 0x52, 0xA9, 0x00, 0x8D, 0x04, 0x52, 0x40]))  # ack, IRQ off
        vectors(prg, 0x20000, 0xE200, irq=0xE400)
        return ines(5, prg, chr_, ram_sizes=ram_sizes)

    def test_5_mmc5_prg(self):  # mode 3: $5114 (bit 7 = ROM) at $8000
        self.assertEqual(peek(self.mmc5(lda_sta(0x03, 0x5100) + lda_sta(0x82, 0x5114) + lda_abs(0x8100) + END)), 0x62)

    def test_5_mmc5_prg_ram_window(self):  # bit 7 clear: PRG RAM; the same 8 KB bank at $6000
        code = (lda_sta(0x03, 0x5100) + lda_sta(0x01, 0x5114) + lda_sta(0x77, 0x8005) +
                lda_sta(0x01, 0x5113) + lda_abs(0x6005) + END)
        self.assertEqual(peek(self.mmc5(code)), 0x77)

    def test_5_mmc5_multiplier(self):
        self.assertEqual(peek(self.mmc5(lda_sta(13, 0x5205) + lda_sta(11, 0x5206) + lda_abs(0x5205) + END)), 143)

    def test_5_mmc5_nametables(self):  # $5105: fill mode, and ExRAM as a nametable
        fill = WAIT_VBLANK + lda_sta(0xFF, 0x5105) + lda_sta(0x42, 0x5106) + ppu_read(0x2010) + END
        self.assertEqual(peek(self.mmc5(fill)), 0x42)
        exram = (WAIT_VBLANK + lda_sta(0x00, 0x5104) + lda_sta(0xAA, 0x5105) + lda_sta(0x99, 0x5C10) +
                 ppu_read(0x2410) + END)
        self.assertEqual(peek(self.mmc5(exram)), 0x99)

    def test_5_mmc5_scanline_irq(self):
        """IRQ at line 100 ($5203), background on: from the vblank flag at
        (241, 1) to line 100 is 41,260 dots = 13,753 CPU cycles; less ~13 for
        the wait loop, $5204 read and CLI, at 8 cycles a turn plus 7 per
        wrap of the low byte (6): ~1,712 turns (the bus plans a catch-up
        every line, so the IRQ is on time without slow accesses)."""
        code = (bytes([0x78]) + lda_sta(0x40, 0x4017) + WAIT_VBLANK + lda_sta(100, 0x5203) +
                lda_sta(0x80, 0x5204) + lda_sta(0x08, 0x2001) +
                bytes([0x2C, 0x02, 0x20, 0x10, 0xFB]) +  # wait for vblank,
                lda_abs(0x5204) + bytes([0x58]))           # drop last frame's pending IRQ, CLI
        code += ram_loop(0xE200 + len(code))
        rom = self.mmc5(code)
        self.assertAlmostEqual(peek(rom, 0) + 256 * peek(rom, 1), 1712, delta=3)

    # ---- Namco 163 (19): $E000 = last bank ----
    @staticmethod
    def n163(code, chr_=b""):
        prg = bytearray(0x20000)
        for b in range(16):
            prg[b * 0x2000 + 0x100] = 0x60 + b
        put(prg, 0x1E200, code)
        put(prg, 0x1E400, bytes([0xA5, 0x10, 0x85, 0x00, 0xA5, 0x11, 0x85, 0x01,  # IRQ: $00/$01 = loop
                                 0xA9, 0x00, 0x8D, 0x00, 0x58, 0x40]))            # IRQ off (+ ack), RTI
        vectors(prg, 0x20000, 0xE200, irq=0xE400)
        return ines(19, prg, chr_)

    def test_19_n163_prg_and_nametable(self):
        self.assertEqual(peek(self.n163(lda_sta(0x03, 0xE000) + lda_abs(0x8100) + END)), 0x63)
        chr_ = bytearray(0x8000)
        for page in range(32):
            chr_[page * 0x400 + 0x10] = 0x80 + page
        # $C000 < $E0: the first nametable is CHR ROM page 5
        code = WAIT_VBLANK + lda_sta(0x05, 0xC000) + ppu_read(0x2010) + END
        self.assertEqual(peek(self.n163(code, chr_)), 0x85)

    def test_19_n163_ram_port(self):  # $F800 bit 7: auto-increment
        code = (lda_sta(0x80, 0xF800) + lda_sta(0x11, 0x4800) + lda_sta(0x22, 0x4800) +
                lda_sta(0x81, 0xF800) + lda_abs(0x4800) + END)
        self.assertEqual(peek(self.n163(code)), 0x22)

    def test_19_n163_irq_on_time(self):  # counter $7FFF - 1000: IRQ after 1,000 cycles = ~125 turns
        start = 0x7FFF - 1000
        code = (bytes([0x78]) + lda_sta(0x40, 0x4017) + lda_sta(start & 0xFF, 0x5000) +
                lda_sta(0x80 | start >> 8, 0x5800) + bytes([0x58]))
        code += ram_loop(0xE200 + len(code))
        rom = self.n163(code)
        self.assertAlmostEqual(peek(rom, 0) + 256 * peek(rom, 1), 125, delta=2)

    # ---- MMC2 (9), MMC4 (10): CHR latches set by reading tile $FD/$FE patterns ----
    def test_9_10_chr_latch(self):
        """$B000 = 4 KB bank 1 (latch $FD), $C000 = bank 2 ($FE, power-on).
        Reading $0FD8 sets $FD; then $0FE9 sets $FE on the MMC4 only (MMC2's
        left table triggers on $0FE8 exactly)."""
        chr_ = bytearray(0x8000)
        for b in range(8):
            chr_[b * 0x1000 + 0x10] = 0xC0 + b
        for mapper, after_fe9 in ((9, 0xC1), (10, 0xC2)):
            prg = bytearray(0x10000)
            for b in range(8):
                prg[b * 0x2000 + 0x100] = 0x60 + b
            code = (WAIT_VBLANK + lda_sta(0x02, 0xA000) + lda_abs(0x8100) + bytes([0x85, 0x01]) +
                    lda_sta(0x01, 0xB000) + lda_sta(0x02, 0xC000) + ppu_read(0x0010) + bytes([0x85, 0x02]) +
                    ppu_read(0x0FD8) + ppu_read(0x0010) + bytes([0x85, 0x03]) +
                    ppu_read(0x0FE9) + ppu_read(0x0010) + END)
            put(prg, 0xE200, code)
            vectors(prg, 0x10000, 0xE200)
            rom = ines(mapper, prg, chr_)
            with self.subTest(mapper=mapper):
                self.assertEqual(peek(rom, 1), 0x62 if mapper == 9 else 0x64)  # 8 KB / 16 KB bank 2
                self.assertEqual(peek(rom, 2), 0xC2)
                self.assertEqual(peek(rom, 3), 0xC1)
                self.assertEqual(peek(rom, 0), after_fe9)

    # ---- Jaleco SS88006 (18): nibble registers, a counter of 4/8/12/16 bits ----
    def test_18_ss88006_prg(self):  # bank $11 from two nibbles at $8000/$8001
        prg = bytearray(0x40000)
        for b in range(32):
            prg[b * 0x2000 + 0x100] = 0x40 + b
        put(prg, 0x3E200, lda_sta(0x01, 0x8000) + lda_sta(0x01, 0x8001) + lda_abs(0x8100) + END)
        vectors(prg, 0x40000, 0xE200)
        self.assertEqual(peek(ines(18, prg)), 0x51)

    def test_18_ss88006_irq_on_time(self):
        """12-bit mode, reload $F3E8: only the low 12 bits ($3E8 = 1000) count,
        so the IRQ comes after ~1,000 cycles = ~125 turns (16 bits: ~7,800)."""
        prg = bytearray(0x8000)
        code = bytes([0x78]) + lda_sta(0x40, 0x4017)
        for i, nib in enumerate((0x8, 0xE, 0x3, 0xF)):
            code += lda_sta(nib, 0xE000 + i)
        code += lda_sta(0x00, 0xF000) + lda_sta(0x03, 0xF001) + bytes([0x58])  # reload, count 12 bits, CLI
        code += ram_loop(0xE200 + len(code))
        put(prg, 0x6200, code)
        put(prg, 0x6400, bytes([0xA5, 0x10, 0x85, 0x00, 0xA5, 0x11, 0x85, 0x01,  # $00/$01 = loop count
                                0xA9, 0x00, 0x8D, 0x01, 0xF0, 0x40]))          # IRQ off (acknowledges), RTI
        vectors(prg, 0x8000, 0xE200, irq=0xE400)
        rom = ines(18, prg)
        self.assertAlmostEqual(peek(rom, 0) + 256 * peek(rom, 1), 125, delta=2)

    # ---- Sunsoft FME-7 (69): command at $8000, parameter at $A000 ----
    @staticmethod
    def fme7(code):
        prg = bytearray(0x10000)
        for b in range(8):
            prg[b * 0x2000 + 0x100] = 0x60 + b
        put(prg, 0xE200, code)
        put(prg, 0xE400, bytes([0xA5, 0x10, 0x85, 0x00, 0xA5, 0x11, 0x85, 0x01,  # $00/$01 = loop count
                                0xA9, 0x0D, 0x8D, 0x00, 0x80, 0xA9, 0x00, 0x8D, 0x00, 0xA0, 0x40]))  # D = 0, RTI
        vectors(prg, 0x10000, 0xE200, irq=0xE400)
        return ines(69, prg)

    def test_69_fme7_prg_and_6000(self):  # command 9: $8000; command 8: ROM bank, then RAM, at $6000
        code = (lda_sta(0x09, 0x8000) + lda_sta(0x03, 0xA000) + lda_abs(0x8100) + bytes([0x85, 0x01]) +
                lda_sta(0x08, 0x8000) + lda_sta(0x05, 0xA000) + lda_abs(0x6100) + bytes([0x85, 0x02]) +
                lda_sta(0x77, 0x6005) +  # ROM: ignored
                lda_sta(0xC0, 0xA000) + lda_sta(0x55, 0x6005) + lda_abs(0x6005) + END)
        rom = self.fme7(code)
        self.assertEqual(peek(rom, 1), 0x63)
        self.assertEqual(peek(rom, 2), 0x65)
        self.assertEqual(peek(rom, 0), 0x55)

    def test_69_fme7_irq_on_time(self):  # counter 1000: wraps after 1,001 cycles = ~125 turns
        code = bytes([0x78]) + lda_sta(0x40, 0x4017)
        for cmd, v in ((0x0E, 1000 & 0xFF), (0x0F, 1000 >> 8), (0x0D, 0x81)):
            code += lda_sta(cmd, 0x8000) + lda_sta(v, 0xA000)
        code += bytes([0x58])
        code += ram_loop(0xE200 + len(code))
        rom = self.fme7(code)
        self.assertAlmostEqual(peek(rom, 0) + 256 * peek(rom, 1), 125, delta=2)

    # ---- Namco 175/340 (210) ----
    def test_210_prg_and_340_mirroring(self):
        """$E800: 8 KB at $A000. Namco 340 (submapper 2): $E000 bits 6-7 = 1
        is vertical mirroring, so $2800 shows what was written at $2000."""
        prg = bytearray(0x10000)
        for b in range(8):
            prg[b * 0x2000 + 0x100] = 0x60 + b
        ppu_write = lda_sta(0x20, 0x2006) + lda_sta(0x00, 0x2006) + lda_sta(0x5A, 0x2007)
        code = (WAIT_VBLANK + lda_sta(0x03, 0xE800) + lda_abs(0xA100) + bytes([0x85, 0x01]) +
                lda_sta(0x40, 0xE000) + ppu_write + ppu_read(0x2800) + END)
        put(prg, 0xE200, code)
        vectors(prg, 0x10000, 0xE200)
        rom = ines(210, prg, bytes(0x2000), submapper=2)
        self.assertEqual(peek(rom, 1), 0x63)
        self.assertEqual(peek(rom, 0), 0x5A)

    # ---- Bandai FCG (16, 159): FCG-1/2 at $6000 (submapper 4), LZ93D50 at $8000 (5) ----
    @staticmethod
    def bandai(code, mapper=16, submapper=5, ram_sizes=None):
        prg = bytearray(0x20000)
        for b in range(8):
            prg[b * 0x4000 + 0x100] = 0x60 + b
        put(prg, 0x1C200, code)  # in the fixed bank, room for long I2C sequences
        base = 0x6000 if submapper == 4 else 0x8000  # IRQ: $00/$01 = loop count, counting off (acknowledges)
        put(prg, 0x1E400, bytes([0xA5, 0x10, 0x85, 0x00, 0xA5, 0x11, 0x85, 0x01]) + lda_sta(0x00, base + 0xA) + b"\x40")
        vectors(prg, 0x20000, 0xC200, irq=0xE400)
        return ines(mapper, prg, bytes(0x2000), submapper=submapper, ram_sizes=ram_sizes)

    def test_16_bandai_prg_and_irq_on_time(self):
        """PRG register 8; IRQ counter 1000: the FCG-1/2 loads the counter
        directly, the LZ93D50 a latch copied by the enable write; the IRQ
        comes at the clock that finds 0, after ~1,000 cycles = ~125 turns."""
        for sub, base in ((4, 0x6000), (5, 0x8000)):
            with self.subTest(submapper=sub):
                rom = self.bandai(lda_sta(0x02, base + 8) + lda_abs(0x8100) + END, submapper=sub)
                self.assertEqual(peek(rom), 0x62)
                code = (bytes([0x78]) + lda_sta(0x40, 0x4017) + lda_sta(1000 & 0xFF, base + 0xB) +
                        lda_sta(1000 >> 8, base + 0xC) + lda_sta(0x01, base + 0xA) + bytes([0x58]))
                code += ram_loop(0xC200 + len(code))
                rom = self.bandai(code, submapper=sub)
                self.assertAlmostEqual(peek(rom, 0) + 256 * peek(rom, 1), 125, delta=2)

    @staticmethod
    def i2c(*ops):
        """6502 code bit-banging $800D (bit 5 SCL, 6 SDA, 7 read) and reading
        SDA at $6000 bit 4. ops: ("start",), ("stop",), ("byte", v) = send v
        MSB first + an acknowledge clock, ("read",) = 8 bits into $00 + NACK."""
        def lines(scl, sda, rd=0):
            return lda_sta(scl << 5 | sda << 6 | rd << 7, 0x800D)
        out = b""
        for op in ops:
            if op[0] == "start":
                out += lines(1, 1) + lines(1, 0) + lines(0, 0)
            elif op[0] == "stop":
                out += lines(0, 0) + lines(1, 0) + lines(1, 1)
            elif op[0] == "byte":
                for k in range(7, -1, -1):
                    b = op[1] >> k & 1
                    out += lines(0, b) + lines(1, b) + lines(0, b)
                out += lines(0, 1, 1) + lines(1, 1, 1) + lines(0, 1, 1)  # the chip's acknowledge
            else:  # LDA $6000 / AND #$10 / CMP #$10 (carry = bit) / ROL $00, per bit
                for _ in range(8):
                    out += lines(0, 1, 1) + lines(1, 1, 1) + bytes([0xAD, 0x00, 0x60, 0x29, 0x10, 0xC9, 0x10, 0x26, 0x00])
                out += lines(0, 1, 1) + lines(1, 1, 1) + lines(0, 1, 1)  # no acknowledge: done
        return out

    def test_16_159_eeprom_write_then_read(self):
        """24C02 (16, NVRAM 256 bytes): device $A0, address 5, data $4B; then
        a random read (address, repeated start, $A1). X24C01 (159): address
        and R/W in one byte. Both read back $4B."""
        c02 = self.i2c(("start",), ("byte", 0xA0), ("byte", 0x05), ("byte", 0x4B), ("stop",),
                       ("start",), ("byte", 0xA0), ("byte", 0x05), ("start",), ("byte", 0xA1), ("read",), ("stop",))
        c01 = self.i2c(("start",), ("byte", 0x05 << 1), ("byte", 0x4B), ("stop",),
                       ("start",), ("byte", 0x05 << 1 | 1), ("read",), ("stop",))
        self.assertEqual(peek(self.bandai(c02 + END[2:], ram_sizes=0x20)), 0x4B)  # END without STA $00
        self.assertEqual(peek(self.bandai(c01 + END[2:], mapper=159, submapper=0)), 0x4B)


if __name__ == "__main__":
    unittest.main()
