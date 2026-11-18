"""The sprite_limit setting (--all-sprites): 10 white 8x8 sprites side by side
on one line. As on the console only the first 8 (OAM order) are drawn;
with every sprite on, all 10. What the game sees ($2002 overflow, the
mapper's fetches, timing) is the same both ways: the CPU cycle count
matches."""
import os
import subprocess
import tempfile
import unittest

from tests.nes.test_mappers import RUNNER, WAIT_VBLANK, ines, lda_sta, put, vectors
from tests.games import built

Y, COUNT = 100, 10


def run(*args):
    prg = bytearray(0x8000)
    code = WAIT_VBLANK + lda_sta(0x3F, 0x2006) + lda_sta(0x00, 0x2006) + lda_sta(0x0F, 0x2007)  # black backdrop
    code += lda_sta(0x3F, 0x2006) + lda_sta(0x11, 0x2006) + lda_sta(0x30, 0x2007)  # sprite colour 1: white
    for k in range(64):  # OAM copy at $0200: sprite k at x = 16k + 8, the rest off screen
        o = (Y, 1, 0, 16 * k + 8) if k < COUNT else (0xFF, 0, 0, 0)
        for j, v in enumerate(o):
            code += lda_sta(v, 0x0200 + k * 4 + j)
    loop = 0x8200 + len(code)
    code += WAIT_VBLANK[:5] + lda_sta(0x02, 0x4014) + lda_sta(0x14, 0x2001)  # OAM DMA, sprites on
    code += bytes([0x4C, loop & 0xFF, loop >> 8])
    put(prg, 0x200, code)
    vectors(prg, 0x8000, 0x8200)
    chr_ = bytearray(0x2000)
    chr_[16:24] = b"\xFF" * 8  # tile 1: solid colour 1
    with tempfile.TemporaryDirectory() as tmp:
        rom, ppm = os.path.join(tmp, "t.nes"), os.path.join(tmp, "t.ppm")
        with open(rom, "wb") as f:
            f.write(ines(0, prg, bytes(chr_)))
        out = subprocess.run([RUNNER, rom, "--frames", "10", "--hash", "--ppm", ppm, *args], check=True,
                             capture_output=True, text=True, timeout=60).stdout
        px = open(ppm, "rb").read()[-256 * 240 * 3:]
    row = Y + 4  # sprites show one line below their Y
    drawn = sum(1 for k in range(COUNT) if px[(row * 256 + 16 * k + 12) * 3] > 128)
    return drawn, out.split("cycles=")[1].split()[0]


@built(RUNNER, "build the C runtime first (see tests/nes/test_cpu.py)")
class TestSpriteLimit(unittest.TestCase):
    def test_limit(self):
        limited, cycles = run()
        every, cycles_all = run("--all-sprites")
        self.assertEqual(limited, 8)
        self.assertEqual(every, COUNT)
        self.assertEqual(cycles, cycles_all)


if __name__ == "__main__":
    unittest.main()
