"""Game Boy PPU and save states: dmg-acid2 (background, window, objects, priorities, flips,
8x16, the 10-per-line limit) and cgb-acid2 (the same in CGB mode: map
attributes, palettes, VRAM bank 1, OAM-order priority) must match their
reference screenshots pixel for pixel. ROMs and screenshots from the
c-sp/game-boy-test-roms zip (tests/fetch_test_roms.py fetch_gb_test_roms)."""
import os
import struct
import subprocess
import tempfile
import unittest
import zlib

from tests.fetch_test_roms import fetch_gb_test_roms
from tests.test_gb_roms import RUNNER


def png_gray(path):
    """Rows of pixel values of a non-interlaced greyscale or paletted PNG
    (any bit depth up to 8): grey levels, or palette indices."""
    with open(path, "rb") as f:
        d = f.read()
    i, idat = 8, b""
    while i < len(d):
        n, t = struct.unpack(">I4s", d[i:i + 8])
        if t == b"IHDR":
            w, h, depth, ctype = struct.unpack(">IIBB", d[i + 8:i + 18])
            assert ctype in (0, 3) and depth <= 8, "greyscale or paletted only"
        elif t == b"IDAT":
            idat += d[i + 8:i + 8 + n]
        i += 12 + n
    raw, stride = zlib.decompress(idat), (w * depth + 7) // 8
    rows, prev, p = [], bytes(stride), 0
    for _ in range(h):
        kind, line = raw[p], bytearray(raw[p + 1:p + 1 + stride])
        p += 1 + stride
        for x in range(stride):  # filters: bytes per pixel is 1 below 8 bits
            a, b, c = (line[x - 1] if x else 0), prev[x], (prev[x - 1] if x else 0)
            if kind == 1: line[x] = (line[x] + a) & 255
            elif kind == 2: line[x] = (line[x] + b) & 255
            elif kind == 3: line[x] = (line[x] + (a + b) // 2) & 255
            elif kind == 4:
                pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                line[x] = (line[x] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        prev = bytes(line)
        rows.append([(line[x * depth // 8] >> (8 - depth - x * depth % 8)) & ((1 << depth) - 1) for x in range(w)])
    return rows


def png_rgb(path):
    """Rows of (r, g, b) of a paletted PNG."""
    with open(path, "rb") as f:
        d = f.read()
    i = d.index(b"PLTE") - 4
    n = struct.unpack(">I", d[i:i + 4])[0]
    pal = [tuple(d[i + 8 + k:i + 11 + k]) for k in range(0, n, 3)]
    return [[pal[v] for v in row] for row in png_gray(path)]


@unittest.skipUnless(os.path.exists(RUNNER), "build gb_headless first")
class TestDmgAcid2(unittest.TestCase):
    def test_matches_reference(self):
        base = os.path.join(fetch_gb_test_roms(), "dmg-acid2")
        ref = png_gray(os.path.join(base, "dmg-acid2-dmg.png"))  # 2-bit: 0 black .. 3 white
        with tempfile.TemporaryDirectory() as tmp:
            out = os.path.join(tmp, "frame.pgm")
            subprocess.run([RUNNER, os.path.join(base, "dmg-acid2.gb"), "--frames", "120", "--pgm", out],
                           capture_output=True, check=False)
            with open(out, "rb") as f:
                pix = f.read().split(b"\n", 3)[3]
        ours = [[pix[y * 160 + x] // 85 for x in range(160)] for y in range(144)]
        bad = [(x, y) for y in range(144) for x in range(160) if ours[y][x] != ref[y][x]]
        self.assertEqual(len(bad), 0, f"{len(bad)} pixels differ, first at {bad[:5]}")


@unittest.skipUnless(os.path.exists(RUNNER), "build gb_headless first")
class TestCgbAcid2(unittest.TestCase):
    def test_matches_reference(self):
        base = os.path.join(fetch_gb_test_roms(), "cgb-acid2")
        ref = png_rgb(os.path.join(base, "cgb-acid2.png"))
        with tempfile.TemporaryDirectory() as tmp:
            out = os.path.join(tmp, "frame.ppm")
            subprocess.run([RUNNER, os.path.join(base, "cgb-acid2.gbc"), "--frames", "120", "--ppm", out],
                           capture_output=True, check=False)
            with open(out, "rb") as f:
                pix = f.read().split(b"\n", 3)[3]
        bad = [(x, y) for y in range(144) for x in range(160)
               if tuple(pix[(y * 160 + x) * 3:(y * 160 + x) * 3 + 3]) != ref[y][x]]
        self.assertEqual(len(bad), 0, f"{len(bad)} pixels differ, first at {bad[:5]}")


@unittest.skipUnless(os.path.exists(RUNNER), "build gb_headless first")
class TestGbStates(unittest.TestCase):
    def test_save_load_continues_identically(self):
        """Running 600 frames straight, or saving at 300, loading and running
        300 more, ends on the same picture (a ROM whose screen keeps changing)."""
        rom = os.path.join(fetch_gb_test_roms(), "blargg", "cpu_instrs", "cpu_instrs.gb")
        with tempfile.TemporaryDirectory() as tmp:
            st, a, b, c = (os.path.join(tmp, n) for n in ("s.state", "a.pgm", "b.pgm", "c.pgm"))
            subprocess.run([RUNNER, rom, "--frames", "600", "--state-at", "300", st, "--pgm", a], capture_output=True)
            subprocess.run([RUNNER, rom, "--load-state", st, "--frames", "300", "--pgm", b], capture_output=True)
            subprocess.run([RUNNER, rom, "--frames", "300", "--pgm", c], capture_output=True)
            with open(a, "rb") as f, open(b, "rb") as g, open(c, "rb") as h:
                straight, resumed, halfway = f.read(), g.read(), h.read()
        self.assertEqual(straight, resumed)
        self.assertNotEqual(straight, halfway, "the screen should change between frame 300 and 600")


if __name__ == "__main__":
    unittest.main()
