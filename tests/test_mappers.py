"""Mappers 71, 79, 87, 185, 206 (runtime/cart.c) on tiny hand-assembled ROMs:
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


def lda_sta(value, addr):
    return bytes([0xA9, value, 0x8D, addr & 0xFF, addr >> 8])


def lda_abs(addr):
    return bytes([0xAD, addr & 0xFF, addr >> 8])


def ines(mapper, prg, chr_=b"", submapper=None):
    hdr = bytearray(b"NES\x1a" + bytes([len(prg) // 0x4000, len(chr_) // 0x2000,
                                        (mapper & 0x0F) << 4, mapper & 0xF0]) + bytes(8))
    if submapper is not None:  # NES 2.0
        hdr[7] |= 0x08
        hdr[8] = submapper << 4
    return bytes(hdr) + bytes(prg) + bytes(chr_)


def put(prg, offset, data):
    prg[offset:offset + len(data)] = data


def vectors(prg, end, reset):  # NMI and IRQ point at an RTI just before the vectors
    put(prg, end - 7, bytes([0x40]))
    rti = 0xFFF9
    put(prg, end - 6, bytes([rti & 0xFF, rti >> 8, reset & 0xFF, reset >> 8, rti & 0xFF, rti >> 8]))


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


if __name__ == "__main__":
    unittest.main()
