"""Famicom Disk System (systems/nes/runtime/fds.c, the FDS sound in systems/nes/runtime/apu.c),
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

from tests.nes.nesrecomp import NESRECOMP
from tests.nes.test_roms import RUNNER
from tests.games import built

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


def disk(files=()):
    """One side; files: (load address, data), PRG files."""
    side = bytearray(65500)
    info = b"\x01*NINTENDO-HVC*" + bytes(range(1, 42))  # 56 bytes: type, magic, the rest numbered
    side[:56] = info
    side[56:58] = bytes([2, len(files)])
    j = 58
    for i, (addr, data) in enumerate(files):
        header = bytes([3, i, i]) + b"FILE%04d" % i + struct.pack("<HHB", addr, len(data), 0)
        side[j:j + 17 + len(data)] = header + b"\x04" + data
        j += 17 + len(data)
    return bytes(side)


def fnv1a(data):
    h = 2166136261
    for b in data:
        h = ((h ^ b) * 16777619) & 0xFFFFFFFF
    return h


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


@built(NESRECOMP, "build nesrecomp first (cmake --build build)")
class TestFdsRecompile(unittest.TestCase):
    def test_disk_files(self):
        """Two files load different code at $6000. A RAM miss names its
        bytes, which pick the file; each file's block checks RAM before
        running, and the dispatcher table lists both at $6000."""
        a = bytes([0xA9, 0x01, 0x8D, 0x00, 0x03, 0x4C, 0x00, 0x60])  # LDA #1 / STA $0300 / JMP $6000
        b = bytes([0xA2, 0x02, 0x8E, 0x05, 0x60, 0x4C, 0x00, 0x60])  # LDX #2 / STX $6005 (itself) / JMP $6000
        with tempfile.TemporaryDirectory() as tmp:
            bi, d, m, h, out = (os.path.join(tmp, n) for n in ("bios.bin", "t.fds", "m.txt", "h.toml", "o.c"))
            open(bi, "wb").write(bios([]))
            open(d, "wb").write(disk([(0x6000, a), (0x6000, b), (0x0100, a)]))  # the stack page: never
            open(m, "w").write("MISS $6000 1 (RAM A2028E)\n")  # file 1's bytes only
            subprocess.run([NESRECOMP, d, "--bios", bi, "--misses", m, "--save-hints", h], check=True,
                           capture_output=True)
            hints = open(h).read()
            self.assertIn("[0x%08X, 0x6000],  # runtime" % fnv1a(b), hints)
            self.assertNotIn("0x%08X" % fnv1a(a), hints)
            with open(h) as f:  # file 0's code by hand
                text = f.read().replace("ram = [", "ram = [\n    [0x%08X, 0x6000]," % fnv1a(a))
            open(h, "w").write(text)
            subprocess.run([NESRECOMP, d, "--bios", bi, "--hints", h, "-o", out], check=True, capture_output=True)
            c = open(out).read()
        self.assertIn("if (!recomp_ram_is(c, 0x6000, k_f0_6000, 8)) return;", c)
        self.assertIn("if (!recomp_ram_is(c, 0x6000, k_f1_6000, 8)) return;", c)
        self.assertIn("{0x6000, 0},\n    {0x6000, 1},", c)
        # STX $6005 changes the block's own operand: checked again before the JMP
        self.assertIn("if (c->poll_prev || !recomp_ram_is(c, 0x6000, k_f1_6000, 8)) return;", c)
        self.assertEqual(c.count("code changed"), 1)  # STA $0300 in file 0 does not reach it
        self.assertIn(".bios = bios_image", c)
        self.assertIn(".rom = rom_image", c)


@built(RUNNER, "build the C runtime first (see tests/nes/test_cpu.py)")
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
