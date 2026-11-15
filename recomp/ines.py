"""iNES / NES 2.0 header parser. Spec: nesdev wiki "INES" and "NES 2.0".

Header (16 bytes): "NES\\x1a", PRG size (16 KB units), CHR size (8 KB units,
0 = CHR-RAM), flags 6, flags 7, ...
flags 6: bit0 mirroring (1 = vertical), bit1 battery, bit2 trainer (512 bytes
before PRG), bit3 four-screen, bits4-7 mapper low nibble.
flags 7: bits2-3 == 0b10 means NES 2.0, bits4-7 mapper high nibble.
"""
import struct


class Cartridge:
    def __init__(self, data: bytes):
        if data[:4] != b"NES\x1a":
            raise ValueError("not an iNES file")
        prg_units, chr_units, f6, f7 = data[4], data[5], data[6], data[7]
        self.nes2 = (f7 & 0x0C) == 0x08
        self.mapper = (f6 >> 4) | (f7 & 0xF0)
        if self.nes2:
            self.mapper |= (data[8] & 0x0F) << 8
            # shortcut: NES 2.0 exponent-multiplier sizes not supported; add when a ROM needs it
            prg_units |= (data[9] & 0x0F) << 8
            chr_units |= (data[9] & 0xF0) << 4
        self.mirroring = "four" if f6 & 0x08 else ("vertical" if f6 & 0x01 else "horizontal")
        self.battery = bool(f6 & 0x02)
        off = 16 + (512 if f6 & 0x04 else 0)
        prg_size, chr_size = prg_units * 0x4000, chr_units * 0x2000
        if prg_size == 0 or len(data) < off + prg_size + chr_size:
            raise ValueError("file shorter than the header says")
        self.prg = data[off:off + prg_size]
        self.chr = data[off + prg_size:off + prg_size + chr_size]
        self.chr_ram = chr_size == 0

    @classmethod
    def load(cls, path):
        with open(path, "rb") as f:
            return cls(f.read())

    def vectors(self):
        """(nmi, reset, irq): the last 6 bytes of PRG are mapped at $FFFA-$FFFF
        by every common mapper at power-on (the last bank is fixed high)."""
        return struct.unpack("<HHH", self.prg[-6:])

    def nrom_image(self):
        """64 KB CPU view for NROM: PRG at $8000, 16 KB mirrored into $C000."""
        if self.mapper != 0:
            raise ValueError(f"mapper {self.mapper} needs bank-aware loading")
        img = bytearray(0x10000)
        img[0x8000:] = self.prg * (0x8000 // len(self.prg))
        return img

    def summary(self):
        nmi, reset, irq = self.vectors()
        return (f"mapper {self.mapper}, PRG {len(self.prg) // 1024} KB, "
                f"CHR {'RAM' if self.chr_ram else f'{len(self.chr) // 1024} KB'}, "
                f"{self.mirroring} mirroring, NMI=${nmi:04X} RESET=${reset:04X} IRQ=${irq:04X}")
