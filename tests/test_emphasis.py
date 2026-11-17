"""Colour emphasis ($2001 bits 5-7, runtime/palette.c): a backdrop of white
($30) with rendering off, read back from nes_headless --ppm. On the NTSC PPU
bit 5 tints red, 6 green, 7 blue; all three dim every colour evenly. PAL and
Dendy swap red and green (nesdev "Cycle reference chart")."""
import os
import subprocess
import tempfile
import unittest

from tests.test_mappers import RUNNER, WAIT_VBLANK, ines, lda_sta, put, vectors


def backdrop_rgb(mask, region=0):
    prg = bytearray(0x8000)
    code = (WAIT_VBLANK + lda_sta(0x3F, 0x2006) + lda_sta(0x00, 0x2006) + lda_sta(0x30, 0x2007) +
            lda_sta(0x00, 0x2006) + lda_sta(0x00, 0x2006) +  # VRAM address off the palette
            lda_sta(mask, 0x2001))  # rendering off
    here = 0x8200 + len(code)
    code += bytes([0x4C, here & 0xFF, here >> 8])  # JMP to itself
    put(prg, 0x200, code)
    vectors(prg, 0x8000, 0x8200)
    with tempfile.TemporaryDirectory() as tmp:
        rom, ppm = os.path.join(tmp, "t.nes"), os.path.join(tmp, "t.ppm")
        with open(rom, "wb") as f:
            data = bytearray(ines(0, prg, bytes(0x2000)))
            data[7] |= 0x08  # NES 2.0: byte 12 is the region
            data[12] = region
            f.write(data)
        subprocess.run([RUNNER, rom, "--frames", "10", "--ppm", ppm], check=True, capture_output=True, timeout=60)
        px = open(ppm, "rb").read()[-256 * 240 * 3:]
    i = (120 * 256 + 128) * 3
    return tuple(px[i:i + 3])


@unittest.skipUnless(os.path.exists(RUNNER), "build the C runtime first (see tests/test_cpu.py)")
class TestEmphasis(unittest.TestCase):
    def test_tints(self):
        white = backdrop_rgb(0x00)
        self.assertEqual(white, (255, 255, 255))
        for mask, channel in ((0x20, 0), (0x40, 1), (0x80, 2)):
            with self.subTest(mask=hex(mask)):
                rgb = backdrop_rgb(mask)
                others = [rgb[k] for k in range(3) if k != channel]
                self.assertGreater(rgb[channel], max(others))  # the emphasized colour leads
        for region in (1, 3):  # PAL, Dendy: bit 5 green, bit 6 red
            for mask, channel in ((0x20, 1), (0x40, 0), (0x80, 2)):
                with self.subTest(region=region, mask=hex(mask)):
                    rgb = backdrop_rgb(mask, region)
                    self.assertGreater(rgb[channel], max(rgb[k] for k in range(3) if k != channel))
        dim = backdrop_rgb(0xE0)
        self.assertTrue(dim[0] == dim[1] == dim[2] < 255, dim)  # all three: an even dimming


if __name__ == "__main__":
    unittest.main()
