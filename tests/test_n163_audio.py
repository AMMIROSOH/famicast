"""Namco 163 wavetable sound (runtime/cart.c cart_n163_clock, mixed by
runtime/apu.c): one channel set up through the $F800/$4800 RAM port, the
output read back from nes_headless --wav (48 kHz, after the NES's own
high-pass filters, so a step in the wave shows as a spike)."""
import os
import struct
import subprocess
import tempfile
import unittest

from tests import test_mappers  # not its TestMappers by name: unittest would run that class here too
from tests.test_mappers import RUNNER, lda_sta

CPU_HZ = 1789773


def n163_wav(wave, length, freq, channels=1, halt=False, frames=150):
    """wave: bytes at sound RAM $00 (two 4-bit samples each, low nibble
    first); channel 8 plays `length` samples from address 0 at `freq`, volume
    15. Returns the 16-bit samples from 0.5 s on (the filters settled)."""
    regs = bytes([freq & 0xFF, 0, (freq >> 8) & 0xFF, 0, (256 - length) | (freq >> 16), 0, 0,
                  (channels - 1) << 4 | 0x0F])
    code = lda_sta(0x80, 0xF800) + b"".join(lda_sta(b, 0x4800) for b in wave)  # auto-increment from $00
    code += lda_sta(0xF8, 0xF800) + b"".join(lda_sta(b, 0x4800) for b in regs)  # $78-$7F
    code += lda_sta(0x40 if halt else 0x00, 0xE000)  # bit 6: halt
    here = 0xE200 + len(code)
    code += bytes([0x4C, here & 0xFF, here >> 8])
    with tempfile.TemporaryDirectory() as tmp:
        rom, wav = os.path.join(tmp, "t.nes"), os.path.join(tmp, "t.wav")
        with open(rom, "wb") as f:
            f.write(test_mappers.TestMappers.n163(code))
        subprocess.run([RUNNER, rom, "--frames", str(frames), "--wav", wav], check=True, capture_output=True, timeout=60)
        with open(wav, "rb") as f:
            data = f.read()[44:]
    x = struct.unpack("<%dh" % (len(data) // 2), data)
    return x[24000:]


def rises(x, level):  # times the signal goes from below `level` to at or above it
    return sum(1 for a, b in zip(x, x[1:]) if a < level <= b)


@unittest.skipUnless(os.path.exists(RUNNER), "build the C runtime first (see tests/test_cpu.py)")
class TestN163Audio(unittest.TestCase):
    SQUARE = bytes([0x00] * 4 + [0xFF] * 4)  # 8 samples of 0, then 8 of 15

    def test_pitch(self):
        """f = CPU clock * freq / (15 * 65536 * length * channels): freq 3,867
        with 16 samples is 440 Hz alone; with 2 channels each runs every 30
        cycles, 220 Hz (channel 7 is silent, volume 0)."""
        for channels in (1, 2):
            with self.subTest(channels=channels):
                x = n163_wav(self.SQUARE, 16, 3867, channels)
                expected = CPU_HZ * 3867 / (15 * 65536 * 16 * channels)
                self.assertAlmostEqual(rises(x, 0) * 48000 / len(x), expected, delta=expected * 0.02)

    def test_halt(self):
        loud = n163_wav(self.SQUARE, 16, 3867)
        quiet = n163_wav(self.SQUARE, 16, 3867, halt=True)
        self.assertGreater(max(map(abs, loud)), 2000)
        self.assertLess(max(map(abs, quiet)), 50)

    def test_nibble_order(self):
        """Samples 0, F, 0, 0, F, F, F, F (low nibble first: bytes F0 00 FF
        FF) step up twice a period; read high nibble first (F, 0, 0, 0, F,
        ...) once. At 12.5 Hz each step is a spike after the high-pass."""
        x = n163_wav(bytes([0xF0, 0x00, 0xFF, 0xFF]), 8, 55)
        per_s = rises(x, max(x) // 3) * 48000 / len(x)
        expected = 2 * CPU_HZ * 55 / (15 * 65536 * 8)
        self.assertAlmostEqual(per_s, expected, delta=expected * 0.15)


if __name__ == "__main__":
    unittest.main()
