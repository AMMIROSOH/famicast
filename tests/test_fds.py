"""Famicom Disk System (runtime/fds.c, the FDS sound in runtime/apu.c),
without the real BIOS: a stand-in "BIOS" assembled here runs at $E000 with
a minimal disk image (disk info block and a file count of 0).
Expected values: nesdev "FDS disk format", "FDS audio" (wave pitch = CPU *
F / 65536 / 64; full volume about 2.4x an APU square at full volume)."""
import math
import os
import struct
import subprocess
import tempfile
import unittest
import wave

from tests.test_roms import RUNNER

CPU_HZ = 1789773


def sta(addr, value):
    return [0xA9, value, 0x8D, addr & 0xFF, addr >> 8]  # LDA #value / STA addr


def bios(code):
    """8 KB with `code` at $E000 (reset), then an endless loop."""
    b = bytearray(0x2000)
    body = bytes(code) + bytes([0x4C, len(code) & 0xFF, 0xE0])  # JMP to itself
    b[:len(body)] = body
    b[-6:] = struct.pack("<HHH", 0xE000, 0xE000, 0xE000)
    return bytes(b)


def disk():
    side = bytearray(65500)
    info = b"\x01*NINTENDO-HVC*" + bytes(range(1, 42))  # 56 bytes: type, magic, the rest numbered
    side[:56] = info
    side[56:58] = b"\x02\x00"  # file count 0
    return bytes(side)


def run(code, *args):
    with tempfile.TemporaryDirectory() as tmp:
        b, d, w = (os.path.join(tmp, n) for n in ("bios.bin", "t.fds", "t.wav"))
        open(b, "wb").write(bios(code))
        open(d, "wb").write(disk())
        out = subprocess.run([RUNNER, d, "--bios", b, "--wav", w, *args], check=True, capture_output=True,
                             text=True, timeout=60).stdout
        with wave.open(w) as f:
            x = struct.unpack("<%dh" % f.getnframes(), f.readframes(f.getnframes()))
    return out, x


def freq(x):
    x = x[24000:]  # skip 0.5 s
    return sum(1 for a, b in zip(x, x[1:]) if a < 0 <= b) * 48000 / len(x)


# poll $4030 bit 1 (byte transferred), store $4031 at $0300+X, 8 bytes
READ8 = [0xA2, 0x00,                    # LDX #0
         0xAD, 0x30, 0x40,              # loop: LDA $4030
         0x29, 0x02,                    # AND #2
         0xF0, 0xF9,                    # BEQ loop
         0xAD, 0x31, 0x40,              # LDA $4031
         0x9D, 0x00, 0x03,              # STA $0300,X
         0xE8, 0xE0, 0x08,              # INX / CPX #8
         0xD0, 0xEE]                    # BNE loop

FDS_SQUARE = (sta(0x4089, 0x80) + sum((sta(0x4040 + i, 63 if i < 32 else 0) for i in range(64)), []) +
              sta(0x4089, 0x00) + sta(0x4080, 0x80 | 32) + sta(0x4087, 0x80) +
              sta(0x4082, 0x00) + sta(0x4083, 0x04))   # F = $400: 436.9 Hz
APU_SQUARE = (sta(0x4015, 0x01) + sta(0x4000, 0xBF) +  # duty 50%, constant volume 15
              sta(0x4002, 0xFF) + sta(0x4003, 0x00))   # t = 255: CPU / (16 * 256) = 436.9 Hz


@unittest.skipUnless(os.path.exists(RUNNER), "build the C runtime first (see tests/test_cpu.py)")
class TestFds(unittest.TestCase):
    def test_drive_reads_first_block(self):
        """Motor on in read mode, gap detection on: after the spin-up and the
        lead-in gap the $80 start mark arrives, then the disk info block."""
        code = sta(0x4025, 0x65) + READ8  # motor, read, ready (gap detection), bit 5
        got = []
        for i in range(8):
            out, _ = run(code, "--frames", "40", "--peek", "%04X" % (0x300 + i))
            got.append(int(out.split("= ")[-1].split()[0], 16))
        self.assertEqual(bytes(got), b"\x80\x01*NINTE")

    def test_wave_pitch(self):
        self.assertAlmostEqual(freq(run(FDS_SQUARE, "--frames", "120")[1]), CPU_HZ * 0x400 / 65536 / 64, delta=1.0)

    def test_level_against_apu(self):
        """A full-volume FDS square over an APU square at 15, same pitch:
        about 2.4, less the FDS's 2 kHz low-pass at 437 Hz (0.977). Compared
        at the fundamental: the output high-pass filters shape the edges,
        which the low-pass has softened."""
        f = CPU_HZ * 0x400 / 65536 / 64

        def fundamental(x):
            x = x[24000:]
            re = sum(v * math.cos(2 * math.pi * f * n / 48000) for n, v in enumerate(x))
            im = sum(v * math.sin(2 * math.pi * f * n / 48000) for n, v in enumerate(x))
            return math.hypot(re, im)
        ratio = fundamental(run(FDS_SQUARE, "--frames", "120")[1]) / fundamental(run(APU_SQUARE, "--frames", "120")[1])
        self.assertAlmostEqual(ratio, 2.4 / math.hypot(1, f / 2000), delta=0.1)


if __name__ == "__main__":
    unittest.main()
