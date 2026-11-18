"""PAL and Dendy timing (NES 2.0 header byte 12: 0 NTSC, 1 PAL, 3 Dendy).
Expected values from nesdev "Cycle reference chart", "APU Noise", "APU DMC"
and "APU Frame Counter"; tiny NROM programs generated here, read back from
nes_headless (--hash prints the CPU cycle count at the last vblank)."""
import os
import subprocess
import tempfile
import unittest
import struct
import wave

from tests.nes.test_apu import make_rom, sta
from tests.nes.test_roms import RUNNER
from tests.games import built

NTSC, PAL, DENDY = 0, 1, 3
CPU_HZ = {NTSC: 1789773, PAL: 1662607, DENDY: 1773448}
DMC0 = {NTSC: 428, PAL: 398, DENDY: 428}     # DMC rate 0
NOISE4 = {NTSC: 64, PAL: 60, DENDY: 64}      # noise period 4
HALF_FRAME = {NTSC: 14915, PAL: 16627, DENDY: 14915}  # CPU cycles, 4-step mode on average


def rom(code, region):
    r = bytearray(make_rom([0x78] + code))  # SEI first
    r[7] |= 0x08   # NES 2.0
    r[12] = region
    p = 16 + 0x3000  # $F000: DMC sample, alternating bits
    r[p:p + 0x100] = b"\x55" * 0x100
    return bytes(r)


def run(code, region, *args):
    with tempfile.TemporaryDirectory() as tmp:
        path, wav = os.path.join(tmp, "t.nes"), os.path.join(tmp, "t.wav")
        with open(path, "wb") as f:
            f.write(rom(code, region))
        r = subprocess.run([RUNNER, path, "--wav", wav, *args], check=True, capture_output=True, text=True, timeout=60)
        return r.stdout, wav_samples(wav)


def wav_samples(path):
    with wave.open(path) as w:
        return struct.unpack("<%dh" % w.getnframes(), w.readframes(w.getnframes()))


def freq(code, region, *args):  # zero crossings after the first 0.5 s
    x = run(code, region, "--frames", "120", *args)[1][24000:]
    return sum(1 for a, b in zip(x, x[1:]) if a < 0 <= b) * 48000 / len(x)


def cycles(code, region, frames, *args):
    out = run(code, region, "--frames", str(frames), "--hash", *args)[0]
    return int(out.split("cycles=")[1].split()[0])


@built(RUNNER, "build the C runtime first (see tests/nes/test_cpu.py)")
class TestRegion(unittest.TestCase):
    def test_frame_timing(self):
        """CPU cycles per frame with rendering on: NTSC 29780.5 (the odd frame
        skips a dot), PAL 106392 / 3.2 = 33247.5, Dendy 106392 / 3 = 35464.
        The first vblank, from the pre-render line: NTSC and PAL at line 241,
        Dendy after 51 post-render lines at 291. A multi-region game (2) runs
        as NTSC."""
        render = sta(0x2001, 0x18)
        for region, per_frame, vbl in ((NTSC, 29780.5, 241), (2, 29780.5, 241), (PAL, 33247.5, 241),
                                         (DENDY, 35464, 291)):
            with self.subTest(region=region):
                n = cycles(render, region, 101) - cycles(render, region, 1)
                self.assertAlmostEqual(n / 100, per_frame, delta=0.05)
                dots = 341 + vbl * 341 + 1  # (pre-render, 0) to (vbl, 1)
                per_dot = 3.2 if region == PAL else 3
                self.assertAlmostEqual(cycles([], region, 1), dots / per_dot, delta=4)

    def test_override(self):
        """--region (the frontend's region setting) runs a ROM as another
        console than its header says: frame length and APU clock follow."""
        render = sta(0x2001, 0x18)
        pulse = sta(0x4015, 0x01) + sta(0x4000, 0xBF) + sta(0x4002, 0xFD) + sta(0x4003, 0x00)
        for header, flag, per_frame, hz in ((NTSC, "pal", 33247.5, CPU_HZ[PAL]), (PAL, "ntsc", 29780.5, CPU_HZ[NTSC]),
                                            (NTSC, "dendy", 35464, CPU_HZ[DENDY])):
            with self.subTest(header=header, region=flag):
                n = (cycles(render, header, 101, "--region", flag) - cycles(render, header, 1, "--region", flag))
                self.assertAlmostEqual(n / 100, per_frame, delta=0.05)
                self.assertAlmostEqual(freq(pulse, header, "--region", flag), hz / (16 * 254), delta=1.0)

    def test_apu_clock_and_tables(self):
        """Pulse pitch follows the region's CPU clock; the DMC (rate 0, a
        $55 sample: a square at CPU / (2 period)) and the periodic noise use
        the PAL period tables on PAL, NTSC's on the Dendy."""
        pulse = sta(0x4015, 0x01) + sta(0x4000, 0xBF) + sta(0x4002, 0xFD) + sta(0x4003, 0x00)
        dmc = sta(0x4010, 0x40) + sta(0x4012, 0xC0) + sta(0x4013, 0x0F) + sta(0x4015, 0x10)
        noise = sta(0x4015, 0x08) + sta(0x400C, 0x3F) + sta(0x400E, 0x84) + sta(0x400F, 0x00)
        noise_ntsc = freq(noise, NTSC)
        for region in (NTSC, PAL, DENDY):
            with self.subTest(region=region):
                hz = CPU_HZ[region]
                self.assertAlmostEqual(freq(pulse, region), hz / (16 * 254), delta=1.0)
                f = freq(dmc, region)
                self.assertAlmostEqual(f, hz / (2 * DMC0[region]), delta=hz / (2 * DMC0[region]) * 0.01)
                ratio = freq(noise, region) / noise_ntsc
                self.assertAlmostEqual(ratio, hz / NOISE4[region] / (CPU_HZ[NTSC] / 64), delta=0.004)

    def test_frame_counter(self):
        """A pulse with length 254 sounds for 254 half frames: the PAL frame
        counter's steps (16627 cycles a half frame) at the PAL clock is 2.54 s,
        NTSC 2.117 s, the Dendy NTSC's steps at its clock 2.136 s."""
        note = sta(0x4015, 0x01) + sta(0x4000, 0x9F) + sta(0x4002, 0x80) + sta(0x4003, 0x08)
        for region in (NTSC, PAL, DENDY):
            with self.subTest(region=region):
                x = run(note, region, "--frames", "180")[1]
                loud = [i for i, v in enumerate(x) if abs(v) > 1000]
                seconds = (loud[-1] - loud[0]) / 48000
                self.assertAlmostEqual(seconds, 254 * HALF_FRAME[region] / CPU_HZ[region], delta=0.008)


if __name__ == "__main__":
    unittest.main()
