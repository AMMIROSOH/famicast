"""Konami VRC7 sound (systems/nes/runtime/apu.c): the custom instrument set up as a pure
sine carrier (the modulator never attacks), then one feature changed at a
time, read back from nes_headless --wav (after the NES's own high-pass
filters). VRC7a: $9010 selects a register, $9030 writes it."""
import math
import os
import struct
import subprocess
import tempfile
import unittest

from tests.nes import test_mappers  # not its TestMappers by name: unittest would run that class here too
from tests.nes.test_mappers import RUNNER, lda_sta
from tests.games import built

WAIT_ONE = bytes([0x2C, 0x02, 0x20, 0x10, 0xFB])  # BIT $2002 / BPL: the next vblank
FNUM, BLOCK = 288, 4                              # 49716 * 288 * 2^4 / 2^19 = 437 Hz


def reg(r, v):
    return lda_sta(r, 0x9010) + lda_sta(v, 0x9030)


def patch(**kw):
    """The custom instrument: modulator silent (attack 0), carrier a sine
    (multiplier 1, attack 15, no decay, sustained). Keyword args override
    bytes as reg0 .. reg7."""
    b = {0: 0x20, 1: 0x21, 2: 0x3F, 3: 0x00, 4: 0x00, 5: 0xF0, 6: 0x00, 7: 0x00}
    b.update({int(k[3:]): v for k, v in kw.items()})
    return b"".join(reg(r, b[r]) for r in range(8))


def note(volume=0, instrument=0, fnum=FNUM, block=BLOCK):  # channel 0 key on
    return reg(0x10, fnum & 0xFF) + reg(0x30, instrument << 4 | volume) + reg(0x20, 0x10 | block << 1 | fnum >> 8)


def wav(code, frames=150, skip=24000):
    code = bytes([0x78]) + lda_sta(0x40, 0x4017) + code  # SEI, no APU frame IRQ
    here = 0xE200 + len(code)
    code += bytes([0x4C, here & 0xFF, here >> 8])
    with tempfile.TemporaryDirectory() as tmp:
        path, out = os.path.join(tmp, "t.nes"), os.path.join(tmp, "t.wav")
        with open(path, "wb") as f:
            f.write(test_mappers.TestMappers.banked(85, code, submapper=2))
        subprocess.run([RUNNER, path, "--frames", str(frames), "--wav", out], check=True, capture_output=True, timeout=60)
        with open(out, "rb") as f:
            data = f.read()[44:]
    return struct.unpack("<%dh" % (len(data) // 2), data)[skip:]


def freq(x):
    return sum(1 for a, b in zip(x, x[1:]) if a < 0 <= b) * 48000 / len(x)


def rms(x):
    return math.sqrt(sum(v * v for v in x) / len(x))


@built(RUNNER, "build the C runtime first (see tests/nes/test_cpu.py)")
class TestVrc7Audio(unittest.TestCase):
    def test_pitch(self):
        """49716 * fnum * 2^block / 2^19 (the wiki's formula): 437 Hz; the
        carrier's multiplier 2 doubles it."""
        expected = 49716 * FNUM * 2 ** BLOCK / 2 ** 19
        self.assertAlmostEqual(freq(wav(patch() + note())), expected, delta=expected * 0.01)
        self.assertAlmostEqual(freq(wav(patch(reg1=0x22) + note())), 2 * expected, delta=expected * 0.02)

    def test_levels(self):
        """Volume is 3 dB a step: 2 is half the amplitude of 0, 4 a quarter.
        The half-wave rectified sine ($03 bit 4) keeps about 0.55 of the
        sine's AC level (0.386 / 0.707)."""
        full = rms(wav(patch() + note(0)))
        self.assertGreater(full, 2000)
        self.assertAlmostEqual(rms(wav(patch() + note(2))) / full, 0.5, delta=0.03)
        self.assertAlmostEqual(rms(wav(patch() + note(4))) / full, 0.25, delta=0.02)
        self.assertAlmostEqual(rms(wav(patch(reg3=0x10) + note(0))) / full, 0.546, delta=0.06)

    def test_modulation(self):
        """A full modulator (attack 15, level 0) bends the carrier into many
        more zero crossings than its pitch; feedback 7 on a quieter
        modulator changes the sound again."""
        plain = freq(wav(patch() + note()))
        self.assertGreater(freq(wav(patch(reg2=0x00, reg4=0xF0) + note())), 2 * plain)
        a = wav(patch(reg2=0x10, reg4=0xF0) + note())
        b = wav(patch(reg2=0x10, reg3=0x07, reg4=0xF0) + note())
        self.assertGreater(rms([u - v for u, v in zip(a, b)]), 0.2 * rms(a))

    def test_envelope_and_key(self):
        """Attack 0 never sounds. Key off with release 15 falls silent;
        with release 0 the note holds. Built-in instruments from the ROM
        table: Organ (8) is a sustained tone and holds its level; Bells (9)
        is percussive and keeps decaying after its attack."""
        self.assertLess(max(map(abs, wav(patch(reg5=0x00) + note()))), 50)
        off = reg(0x20, BLOCK << 1 | FNUM >> 8)
        self.assertLess(max(map(abs, wav(patch(reg7=0x0F) + note() + WAIT_ONE * 10 + off))), 50)
        self.assertGreater(rms(wav(patch(reg7=0x00) + note() + WAIT_ONE * 10 + off)), 2000)
        for inst, holds in ((8, True), (9, False)):
            with self.subTest(instrument=inst):
                x = wav(note(instrument=inst), skip=0)
                early, late = rms(x[24000:48000]), rms(x[96000:120000])
                self.assertGreater(early, 500)
                if holds:
                    self.assertGreater(late / early, 0.8)
                else:
                    self.assertLess(late / early, 0.5)

    def test_silence(self):
        """$E000 bit 6 silences the chip and clears its registers: clearing
        the bit and keying on again finds no instrument or pitch (the
        cleared custom instrument has attack 0)."""
        code = (patch() + note() + WAIT_ONE * 5 + lda_sta(0x40, 0xE000) + WAIT_ONE + lda_sta(0x00, 0xE000) +
                reg(0x20, BLOCK << 1 | FNUM >> 8) + reg(0x20, 0x10 | BLOCK << 1 | FNUM >> 8))  # key off, on
        self.assertLess(max(map(abs, wav(code))), 50)


if __name__ == "__main__":
    unittest.main()
