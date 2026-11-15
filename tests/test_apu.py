"""APU audio output: pitch of pulse and triangle against the period formulas
(nesdev "APU Pulse": f = CPU / (16 (t+1)); "APU Triangle": f = CPU / (32 (t+1))).
Uses a tiny NROM program generated here, so no external ROM is needed.
The timing behaviour of the APU is covered by the blargg ROMs (test_roms.py)."""
import os
import struct
import subprocess
import tempfile
import unittest
import wave

from tests.test_roms import RUNNER

CPU_HZ = 1789773


def make_rom(code):
    """NROM-128 with `code` at $C000 (reset), then an endless loop."""
    prg = bytearray([0xEA] * 0x4000)
    body = bytes(code) + bytes([0x4C, len(code) & 0xFF, 0xC0])  # JMP to itself
    prg[:len(body)] = body
    prg[-6:] = struct.pack("<HHH", 0xC000, 0xC000, 0xC000)
    return b"NES\x1a" + bytes([1, 1, 0, 0]) + bytes(8) + bytes(prg) + bytes(0x2000)


def sta(addr, value):
    return [0xA9, value, 0x8D, addr & 0xFF, addr >> 8]  # LDA #value / STA addr


def frequency(path):
    with wave.open(path) as w:
        n = w.getnframes()
        rate = w.getframerate()
        s = struct.unpack("<%dh" % n, w.readframes(n))
    s = s[rate // 2:]  # skip the first 0.5 s (filters settling)
    crossings = sum(1 for a, b in zip(s, s[1:]) if a < 0 <= b)
    return crossings * rate / len(s)


@unittest.skipUnless(os.path.exists(RUNNER), "build the C runtime first (see tests/test_cpu.py)")
class TestApuPitch(unittest.TestCase):
    def run_tone(self, code):
        with tempfile.TemporaryDirectory() as tmp:
            rom, wav = os.path.join(tmp, "tone.nes"), os.path.join(tmp, "tone.wav")
            with open(rom, "wb") as f:
                f.write(make_rom([0x78] + code))  # SEI first
            subprocess.run([RUNNER, rom, "--frames", "120", "--wav", wav], check=True,
                           capture_output=True)
            return frequency(wav)

    def test_pulse_440(self):
        t = 253  # 1789773 / (16 * 254) = 440.4 Hz
        f = self.run_tone(sta(0x4015, 0x01) + sta(0x4000, 0xBF) + sta(0x4001, 0x00) +
                          sta(0x4002, t & 0xFF) + sta(0x4003, t >> 8))
        self.assertAlmostEqual(f, CPU_HZ / (16 * (t + 1)), delta=2.0)

    def test_triangle_440(self):
        t = 126  # 1789773 / (32 * 127) = 440.4 Hz
        f = self.run_tone(sta(0x4015, 0x04) + sta(0x4008, 0xFF) +
                          sta(0x400A, t & 0xFF) + sta(0x400B, t >> 8))
        self.assertAlmostEqual(f, CPU_HZ / (32 * (t + 1)), delta=2.0)

    def test_silence_when_disabled(self):
        f = self.run_tone(sta(0x4015, 0x00) + sta(0x4000, 0xBF) + sta(0x4002, 0xFD) + sta(0x4003, 0x00))
        self.assertEqual(f, 0)


if __name__ == "__main__":
    unittest.main()
