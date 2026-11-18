"""iNES loading as the recompiler sees it (nes/runtime/cart.c, via nesrecomp --info)."""
import os
import tempfile
import unittest

from tests.nes.nesrecomp import BUILT, make_rom, run
from tests.games import built


def info(rom):
    with tempfile.TemporaryDirectory() as tmp:
        path = os.path.join(tmp, "t.nes")
        with open(path, "wb") as f:
            f.write(rom)
        r = run("--info", path, check=False)
        return r.returncode, (r.stdout + r.stderr).strip()


@built(BUILT, "build nesrecomp first (cmake --build build)")
class TestInes(unittest.TestCase):
    def test_nrom128(self):
        self.assertEqual(info(make_rom(f6=0x01)), (0, "mapper 0, PRG 16 KB, CHR 8 KB, vertical mirroring, "
                                                      "NMI=$D400 RESET=$C070 IRQ=$C070"))

    def test_mapper_high_nibble(self):
        code, text = info(make_rom(f6=0x40, f7=0x60))
        self.assertEqual(code, 2)
        self.assertIn("mapper 100 not supported", text)

    def test_nes2_mapper_bits_8_to_11(self):
        rom = bytearray(make_rom(f6=0x30, f7=0x08))  # NES 2.0, low byte 3
        rom[8] = 0x02  # mapper 515, not CNROM
        code, text = info(bytes(rom))
        self.assertEqual(code, 2)
        self.assertIn("mapper 515 not supported", text)

    def test_trainer_and_chr_ram(self):
        code, text = info(make_rom(prg_units=8, chr_units=0, f6=0x20, trainer=True))
        self.assertEqual(code, 0)
        self.assertIn("mapper 2, PRG 128 KB, CHR RAM", text)
        self.assertIn("RESET=$C070", text)  # trainer skipped

    def test_diskdude_junk_header(self):
        rom = bytearray(make_rom(f6=0x21))
        rom[7:16] = b"DiskDude!"  # byte 7 'D' would add $40 to the mapper
        self.assertIn("mapper 2,", info(bytes(rom))[1])

    def test_truncated(self):
        code, text = info(make_rom()[:-1])
        self.assertEqual(code, 2)
        self.assertIn("shorter", text)


if __name__ == "__main__":
    unittest.main()
