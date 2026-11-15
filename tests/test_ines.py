import unittest

from recomp.ines import Cartridge


def make_rom(prg_units=1, chr_units=1, f6=0, f7=0, trainer=False):
    hdr = b"NES\x1a" + bytes([prg_units, chr_units, f6 | (0x04 if trainer else 0), f7]) + bytes(8)
    prg = bytearray(prg_units * 0x4000)
    prg[-6:] = bytes([0x00, 0xD4, 0x70, 0xC0, 0x70, 0xC0])  # NMI $D400, RESET/IRQ $C070
    return hdr + (bytes(512) if trainer else b"") + bytes(prg) + bytes(chr_units * 0x2000)


class TestInes(unittest.TestCase):
    def test_nrom128(self):
        c = Cartridge(make_rom(f6=0x01))
        self.assertEqual((c.mapper, c.mirroring, c.chr_ram), (0, "vertical", False))
        self.assertEqual(c.vectors(), (0xD400, 0xC070, 0xC070))
        img = c.nrom_image()
        self.assertEqual(img[0xBFFA:0xC000], img[0xFFFA:0x10000])  # 16 KB mirrored

    def test_mapper_nibbles_trainer_chr_ram(self):
        c = Cartridge(make_rom(prg_units=8, chr_units=0, f6=0x10, f7=0x40, trainer=True))
        self.assertEqual(c.mapper, 0x41)
        self.assertTrue(c.chr_ram)
        self.assertEqual(c.vectors()[1], 0xC070)  # trainer skipped

    def test_truncated(self):
        with self.assertRaises(ValueError):
            Cartridge(make_rom()[:-1])


if __name__ == "__main__":
    unittest.main()
