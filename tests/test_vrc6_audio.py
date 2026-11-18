"""Konami VRC6 sound (nes/runtime/apu.c): two pulses with 16-step duty and a
sawtooth, read back from nes_headless --wav (after the NES's own high-pass
filters: a constant level fades to 0)."""
import math
import os
import struct
import subprocess
import tempfile
import unittest

from tests import test_mappers  # not its TestMappers by name: unittest would run that class here too
from tests.test_mappers import RUNNER, lda_sta

CPU_HZ = 1789773


def wav(code, mapper=24, frames=150):
    """16-bit samples from 0.5 s on (the filters settled); `code` writes
    registers as on mapper 24 (w() maps them for 26)."""
    code = bytes([0x78]) + lda_sta(0x40, 0x4017) + code  # SEI, no APU frame IRQ
    here = 0xE200 + len(code)
    code += bytes([0x4C, here & 0xFF, here >> 8])
    with tempfile.TemporaryDirectory() as tmp:
        path, out = os.path.join(tmp, "t.nes"), os.path.join(tmp, "t.wav")
        with open(path, "wb") as f:
            f.write(test_mappers.TestMappers.banked(mapper, code))
        subprocess.run([RUNNER, path, "--frames", str(frames), "--wav", out], check=True, capture_output=True, timeout=60)
        with open(out, "rb") as f:
            data = f.read()[44:]
    return struct.unpack("<%dh" % (len(data) // 2), data)[24000:]


def w(a, v, mapper=24):
    return lda_sta(v, test_mappers.TestMappers.vrc6_addr(mapper, a))


def pulse(control, period=253, mapper=24):  # pulse 1: $9000 duty/volume, period, enabled
    return (w(0x9003, 0, mapper) + w(0x9000, control, mapper) + w(0x9001, period & 0xFF, mapper) +
            w(0x9002, 0x80 | period >> 8, mapper))


def freq(x):  # upward zero crossings per second
    return sum(1 for a, b in zip(x, x[1:]) if a < 0 <= b) * 48000 / len(x)


def rms(x):
    return math.sqrt(sum(v * v for v in x) / len(x))


@unittest.skipUnless(os.path.exists(RUNNER), "build the C runtime first (see tests/test_cpu.py)")
class TestVrc6Audio(unittest.TestCase):
    def test_pulse(self):
        """Clock / (16 * (t + 1)): 3,496 Hz at t = 31; 440 Hz at t = 253, on
        24 and on 26 (whose $9001/$9002 are swapped). Duty D is D + 1 of 16
        steps high: the AC
        level rises with D, D = 0 (1/16) at about half of D = 7 (8/16;
        ideally 0.48, 0.56 after the NES filters) and D = 3 (4/16) between
        (0.87, 0.92 filtered). Bit 7 (mode) holds the volume: a constant,
        filtered away. Volume is linear: 15 is three times 5. At 15 and half
        duty it is about as loud as an APU pulse at 15."""
        expected = CPU_HZ / (16 * 254)
        high = CPU_HZ / (16 * 32)  # t = 31: the + 1 is 3% here
        self.assertAlmostEqual(freq(wav(pulse(0x7F, 31))), high, delta=high * 0.01)
        for mapper in (24, 26):
            with self.subTest(mapper=mapper):
                self.assertAlmostEqual(freq(wav(pulse(0x7F, mapper=mapper), mapper)), expected, delta=expected * 0.02)
        half = rms(wav(pulse(0x7F)))
        levels = [rms(wav(pulse(d << 4 | 15))) / half for d in range(8)]
        self.assertEqual(levels, sorted(levels))
        self.assertTrue(0.45 < levels[0] < 0.65 and 0.82 < levels[3] < 0.97, levels)
        self.assertLess(max(map(abs, wav(pulse(0xFF)))), 50)
        self.assertAlmostEqual(half / rms(wav(pulse(0x75))), 3.0, delta=0.1)
        apu = lda_sta(0x01, 0x4015) + lda_sta(0xBF, 0x4000) + lda_sta(0x08, 0x4001) + lda_sta(0xFD, 0x4002) + lda_sta(0x00, 0x4003)
        self.assertAlmostEqual(half / rms(wav(apu)), 1.0, delta=0.05)

    def test_saw(self):
        """Clock / (14 * (t + 1)): 440 Hz at t = 290. The accumulator adds
        its rate every second clock and clears on the 14th: at rate 42 (the
        most that does not wrap: 6 * 42 = 252) one ramp per cycle; adding
        every clock would wrap mid-cycle and double the pitch."""
        x = wav(w(0x9003, 0) + w(0xB000, 42) + w(0xB001, 290 & 0xFF) + w(0xB002, 0x80 | 290 >> 8))
        expected = CPU_HZ / (14 * 291)
        self.assertAlmostEqual(freq(x), expected, delta=expected * 0.02)

    def test_frequency_control(self):
        """$9003 bit 1 shifts periods right by 4 (t = $FDF plays as 253:
        440 Hz); bit 0 halts every channel."""
        x = wav(pulse(0x7F, 0xFDF) + w(0x9003, 0x02))
        self.assertAlmostEqual(freq(x), CPU_HZ / (16 * 254), delta=9)
        self.assertLess(max(map(abs, wav(pulse(0x7F) + w(0x9003, 0x01)))), 50)


if __name__ == "__main__":
    unittest.main()
