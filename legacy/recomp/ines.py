# -*- coding: utf-8 -*-
"""Minimal iNES parser (header + PRG/CHR). NROM mapper 0 target first; the
mapper number is carried for the driver."""

import struct

class Cartridge:
    def __init__(self, path):
        raw = open(path, "rb").read()
        if raw[:4] != b"NES\x1a":
            raise ValueError("not an iNES file")
        size, flags, i6, i7 = raw[4], raw[5], raw[6], raw[7]
        self.prg_banks = size or 1
        self.chr_banks = (flags & 0x0F) or 1
        self.mapper = ((flags >> 2) & 0x0F) | (((i6 >> 0) & 0x0F) << 4)
        self.flags6, self.flags7 = i6, i7

        # iNES: flags bit0=1 means NO trainer. Many old dumps get this wrong;
        # validate via the vector words (must sit in the 0x8000-0xFFFD PRG
        # window) and fall back to the other layout if the flag lies.
        def _vecs(off):
            n = self.prg_banks * 0x4000
            if off + n > len(raw):
                return None
            return struct.unpack("<HHH", raw[off + n - 6:off + n])

        def _ok(v):
            return v is not None and all(0x8000 <= w <= 0xFFFD for w in v)

        claimed_trainer = (flags & 1) == 0
        off = 16 + (512 if claimed_trainer else 0)
        self.trainer_present = claimed_trainer
        if not _ok(_vecs(off)):  # header flag lies -> flip the layout
            off = 16 + (0 if claimed_trainer else 512)
            self.trainer_present = not claimed_trainer
        self.prgraw_off = off
        self.prg = raw[off:off + self.prg_banks * 0x4000]
        off += self.prg_banks * 0x4000
        if not (flags & 0x10):  # mapper 2 (CHR RAM) when 0 chr banks
            self.chr = raw[off:off + self.chr_banks * 0x2000]
        else:
            self.chr = bytes(0x2000 * self.chr_banks)
        self.trainer = raw[16:16 + 512] if self.trainer_present else None

    # NROM mapping: each 16K PRG bank sits at 0x8000 and the whole PRG region
    # (0x8000-0xFFFF) is mirrored. 1 bank (16K) -> mirrors 4x (Battle City);
    # 2 banks (32K) -> 1x; 4+ banks fill 0x8000-$FFFF directly. The vector
    # words are the last 8 bytes of the PRG data and address 0xFFFA on-hw.
    def prg_ram(self):
        n = self.prg_banks * 0x4000
        if n >= 0x8000:
            return self.prg
        return self.prg * (0x8000 // n)   # mirror to fill 32K

    def vectors(self):
        # NMI@0xFFFA, RESET@0xFFFC, IRQ@0xFFFE: 6 bytes, last of the PRG.
        return struct.unpack("<HHH", self.prg[-6:])  # (nmi, reset, irq)

    def ram_image(self):
        """Full 64K address space as the runtime expects: PRG-RAM at 0x8000+,"""
        img = bytearray(0x10000)
        img[0x8000:0x10000] = self.prg_ram()
        return img

    def summary(self):
        nmi, reset, irq = self.vectors()
        tr = "trainer" if self.trainer_present else "no-trainer"
        return (f"mapper={self.mapper} prg_banks={self.prg_banks} "
                f"chr_banks={self.chr_banks} {tr} vectors: NMI=$%04X RESET=$%04X IRQ=$%04X"
                % (nmi, reset, irq))
