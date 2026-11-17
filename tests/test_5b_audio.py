"""Sunsoft 5B sound (runtime/apu.c): registers through $C000 (select) and
$E000 (data) on mapper 69, read back from nes_headless --wav (after the
NES's own high-pass filters: a constant level fades to 0)."""
import math
import os
import struct
import subprocess
import tempfile
import unittest

from tests import test_mappers  # not its TestMappers by name: unittest would run that class here too
from tests.test_mappers import RUNNER, lda_sta

CPU_HZ = 1789773
WAIT_ONE = bytes([0x2C, 0x02, 0x20, 0x10, 0xFB])  # BIT $2002 / BPL: the next vblank


def reg(r, v):
    return lda_sta(r, 0xC000) + lda_sta(v, 0xE000)


def wav(code, frames=150, skip=24000):
    """16-bit samples from `skip` on (0.5 s: the filters settled)."""
    code = bytes([0x78]) + lda_sta(0x40, 0x4017) + code  # SEI, no APU frame IRQ
    here = 0xE200 + len(code)
    code += bytes([0x4C, here & 0xFF, here >> 8])
    with tempfile.TemporaryDirectory() as tmp:
        path, out = os.path.join(tmp, "t.nes"), os.path.join(tmp, "t.wav")
        with open(path, "wb") as f:
            f.write(test_mappers.TestMappers.fme7(code))
        subprocess.run([RUNNER, path, "--frames", str(frames), "--wav", out], check=True, capture_output=True, timeout=60)
        with open(out, "rb") as f:
            data = f.read()[44:]
    return struct.unpack("<%dh" % (len(data) // 2), data)[skip:]


def rises(x, level):
    return sum(1 for a, b in zip(x, x[1:]) if a < level <= b)


def corr(x, lag, n=12000):  # normalized autocorrelation at `lag` samples
    a, b = x[:n], x[lag:lag + n]
    return sum(u * v for u, v in zip(a, b)) / math.sqrt(sum(u * u for u in a) * sum(v * v for v in b))


def rms(x):
    return math.sqrt(sum(v * v for v in x) / len(x))


def tone_a(volume, period=254):  # channel A's tone only (mixer $FE: noise off, B and C off)
    return reg(0, period & 0xFF) + reg(1, period >> 8) + reg(7, 0xFE) + reg(8, volume)


@unittest.skipUnless(os.path.exists(RUNNER), "build the C runtime first (see tests/test_cpu.py)")
class Test5bAudio(unittest.TestCase):
    def test_tone(self):
        """Clock / (32 * period): period 254 is 220 Hz. The first swing is
        negative (the 5B's signals go from rest to negative). Volume 13 is
        two 3 dB steps below 15: half the amplitude."""
        x = wav(tone_a(15))
        expected = CPU_HZ / (32 * 254)
        self.assertAlmostEqual(rises(x, 0) * 48000 / len(x), expected, delta=expected * 0.02)
        start = wav(WAIT_ONE * 15 + tone_a(15), 40, 7200)
        self.assertLess(next(v for v in start if abs(v) > 1000), 0)
        self.assertAlmostEqual(rms(x) / rms(wav(tone_a(13))), 10 ** (6 / 20), delta=0.1)

    def test_constant_noise_and_write_disable(self):
        """Tone and noise both disabled: a constant level (filtered away).
        Noise alone on A (mixer $F7): sound. A select with the high nibble
        set ($18) blocks the data write: volume stays 0."""
        self.assertLess(max(map(abs, wav(reg(7, 0xFF) + reg(8, 15)))), 50)
        self.assertGreater(rms(wav(reg(6, 0x1F) + reg(7, 0xF7) + reg(8, 15))), 1000)
        blocked = tone_a(0) + lda_sta(0x18, 0xC000) + lda_sta(15, 0xE000)
        self.assertLess(max(map(abs, wav(blocked))), 50)

    def test_envelope(self):
        """Volume bit 4 takes the envelope. Shape $0E (attack/decay
        triangle) on a constant channel repeats every 1,024 * period cycles
        (87 Hz with period 20: 549 samples), and not at half that (a
        sawtooth, if alternate were ignored). The holding shapes gate a 220
        Hz tone: $0D (attack, hold) and $0B (decay, alternate, hold) end
        high; $09, $0F and $04 (continue off: one ramp, then low) end low.
        Held high is level 31, the same as fixed volume 15 (level 2v + 1)."""
        x = wav(reg(7, 0xFF) + reg(0x0B, 20) + reg(0x0C, 0) + reg(8, 0x10) + reg(0x0D, 0x0E))
        period = round(48000 * 1024 * 20 / CPU_HZ)
        self.assertGreater(corr(x, period), 0.95)
        self.assertLess(corr(x, period // 2), 0.5)
        for shape, loud in ((0x0D, True), (0x0B, True), (0x09, False), (0x0F, False), (0x04, False)):
            with self.subTest(shape=hex(shape)):
                y = wav(tone_a(0x10) + reg(0x0B, 1) + reg(0x0C, 0) + reg(0x0D, shape))
                if loud:
                    self.assertGreater(rms(y), 1000)
                else:
                    self.assertLess(max(map(abs, y)), 50)
        held = wav(tone_a(0x10) + reg(0x0B, 1) + reg(0x0C, 0) + reg(0x0D, 0x0D))
        self.assertAlmostEqual(rms(wav(tone_a(15))) / rms(held), 1.0, delta=0.03)


if __name__ == "__main__":
    unittest.main()
