"""MMC5 sound (nes/runtime/apu.c): two pulses without sweep, envelope and length
at ~240 Hz, the 8-bit PCM DAC and its $00 IRQ. Sound is read back from
nes_headless --wav (after the NES's own high-pass filters), registers with
--peek."""
import os
import struct
import subprocess
import tempfile
import unittest

from tests import test_mappers  # not its TestMappers by name: unittest would run that class here too
from tests.test_mappers import RUNNER, WAIT_VBLANK, lda_abs, lda_sta, peek

CPU_HZ = 1789773
WAIT_ONE = bytes([0x2C, 0x02, 0x20, 0x10, 0xFB])  # BIT $2002 / BPL: the next vblank


PREFIX = bytes([0x78]) + lda_sta(0x40, 0x4017)  # SEI, no APU frame IRQ
BASE = 0xE200 + len(PREFIX)                      # where the test's code starts


def rom(code):
    return test_mappers.TestMappers.mmc5(PREFIX + code)


def stop(code):  # code, then a loop on itself (A is not stored)
    here = BASE + len(code)
    return code + bytes([0x4C, here & 0xFF, here >> 8])


def wav(code, frames=150):
    """16-bit samples from 0.5 s on (the filters settled)."""
    loop = BASE + len(code)
    with tempfile.TemporaryDirectory() as tmp:
        path, out = os.path.join(tmp, "t.nes"), os.path.join(tmp, "t.wav")
        with open(path, "wb") as f:
            f.write(rom(code + bytes([0x4C, loop & 0xFF, loop >> 8])))
        subprocess.run([RUNNER, path, "--frames", str(frames), "--wav", out], check=True, capture_output=True, timeout=60)
        with open(out, "rb") as f:
            data = f.read()[44:]
    return struct.unpack("<%dh" % (len(data) // 2), data)


def rises(x, level):
    return sum(1 for a, b in zip(x, x[1:]) if a < level <= b)


# pulse 1: 50% duty, length halted, constant volume 15, period 253 (440 Hz)
PULSE = lda_sta(0x01, 0x5015) + lda_sta(0xBF, 0x5000) + lda_sta(0xFD, 0x5002) + lda_sta(0x00, 0x5003)


@unittest.skipUnless(os.path.exists(RUNNER), "build the C runtime first (see tests/test_cpu.py)")
class TestMmc5Audio(unittest.TestCase):
    def test_pulse_pitch_and_enable(self):
        """f = CPU clock / (16 * (period + 1)), as the APU's pulses; $5015 = 0
        silences it."""
        x = wav(PULSE)[24000:]
        expected = CPU_HZ / (16 * 254)
        self.assertAlmostEqual(rises(x, 0) * 48000 / len(x), expected, delta=expected * 0.02)
        quiet = wav(PULSE + lda_sta(0x00, 0x5015))[24000:]
        self.assertLess(max(map(abs, quiet)), 50)

    def test_polarity(self):
        """MMC5 output is inverted against the APU: a note's first step (the
        duty sequence going high) swings the output negative. The same
        writes to the APU's pulse 1 swing it positive. The note starts 15
        frames in, past the power-on swing."""
        def first_swing(x):
            return next(v for v in x[7200:] if abs(v) > 1000)
        self.assertLess(first_swing(wav(WAIT_ONE * 15 + PULSE, 40)), 0)
        apu = lda_sta(0x01, 0x4015) + lda_sta(0xBF, 0x4000) + lda_sta(0x08, 0x4001) + lda_sta(0xFD, 0x4002) + lda_sta(0x00, 0x4003)
        self.assertGreater(first_swing(wav(WAIT_ONE * 15 + apu, 40)), 0)

    def test_length_at_240hz(self):
        """Length 10 ($5003 index 0), not halted, clocked every 7,424 CPU
        cycles: $5015 bit 0 clears after 9 to 10 clocks (the first comes
        part-way through the divider). Counted by a 16-cycle loop polling
        $5015; at the APU's 120 Hz it would take twice as long."""
        pre = (WAIT_VBLANK + lda_sta(0x01, 0x5015) + lda_sta(0x9F, 0x5000) + lda_sta(0xFD, 0x5002) +
               lda_sta(0x00, 0x5003))
        start = BASE + len(pre)
        # LDA $5015 / AND #1 / BEQ out / INC $10 / BNE start / INC $11 / JMP start
        loop = bytes([0xAD, 0x15, 0x50, 0x29, 0x01, 0xF0, 0x07, 0xE6, 0x10, 0xD0, 0xF5, 0xE6, 0x11,
                      0x4C, start & 0xFF, start >> 8])
        r = rom(stop(pre + loop))
        cycles = 16 * (peek(r, 0x10) + 256 * peek(r, 0x11))
        self.assertGreater(cycles, 9 * 7424 - 7424 // 2)
        self.assertLess(cycles, 10 * 7424)

    def test_pcm_irq(self):
        """$5011 = $00 trips the IRQ (with $5010 bit 7 on); reading $5010
        returns it in bit 7 and acknowledges. With I clear and no read, the
        IRQ handler runs (it copies $10 to $00)."""
        trip = lda_sta(0x80, 0x5010) + lda_sta(0x00, 0x5011)
        acked = stop(trip + lda_abs(0x5010) + bytes([0x85, 0x02]) + lda_abs(0x5010) + bytes([0x85, 0x03]) +
                     lda_sta(0xAA, 0x10) + bytes([0x58]))
        r = rom(acked)
        self.assertEqual(peek(r, 2) & 0x81, 0x80)
        self.assertEqual(peek(r, 3) & 0x81, 0x00)
        self.assertEqual(peek(r, 0), 0x00)  # acknowledged: no IRQ after CLI
        self.assertEqual(peek(rom(stop(lda_sta(0xAA, 0x10) + trip + bytes([0x58]))), 0), 0xAA)

    def test_pcm_dac(self):
        """A $5011 write moves the output (a spike after the high-pass): $FF
        after $01 swings it negative (inverted), several times as far as
        the loudest pulse (like the DMC's full range, doubled by the 8th
        bit); the filters' overshoot back is much smaller."""
        x = wav(WAIT_VBLANK + lda_sta(0x01, 0x5011) + WAIT_ONE * 40 + lda_sta(0xFF, 0x5011), 60)[24000:]
        pulse = wav(WAIT_ONE * 40 + PULSE, 60)[24000:]
        self.assertGreater(-min(x), 3 * (max(pulse) - min(pulse)))
        self.assertGreater(-min(x), 5 * max(x))
        # $00 leaves the DAC alone (it only trips the IRQ): no step at frame 40
        y = wav(WAIT_VBLANK + lda_sta(0xFF, 0x5011) + WAIT_ONE * 40 + lda_sta(0x00, 0x5011), 60)[24000:]
        self.assertLess(max(map(abs, y)), 50)


if __name__ == "__main__":
    unittest.main()
