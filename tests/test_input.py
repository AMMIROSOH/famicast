"""Controller input and input recordings (docs/05 M4). Uses Battle City when
available locally (tests/games.py), since no fetched test ROM reads the pads."""
import os
import struct
import subprocess
import tempfile
import unittest

from tests.games import BATTLE_CITY, find_rom
from tests.test_ppu import RUNNER

START, SELECT, A = 0x08, 0x04, 0x01
PRESSES = [(300, START), (700, START), (760, 0x10), (800, A)]  # menu, game, up, fire


def run(*args):
    r = subprocess.run([RUNNER, *args], capture_output=True, text=True, timeout=300)
    return r.stdout.split("HASH ")[-1].strip()


def write_log(path, frames, presses, reset_at=None):
    """Write a recording the way runtime/input_log.c expects it."""
    with open(path, "wb") as f:
        f.write(b"NESI\x01")
        for i in range(frames):
            pad = 0
            for start, mask in presses:
                if start <= i < start + 6:
                    pad |= mask
            f.write(struct.pack("BBB", pad, 0, 1 if i == reset_at else 0))


@unittest.skipUnless(os.path.exists(RUNNER), "build the C runtime first (see tests/test_cpu.py)")
class TestInput(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.rom = find_rom(BATTLE_CITY)
        if not cls.rom:
            raise unittest.SkipTest("Battle City ROM not found (set NES_GAMES_DIR)")
        cls.tmp = tempfile.mkdtemp()

    def test_record_then_replay_is_identical(self):
        log = os.path.join(self.tmp, "rec.nesi")
        press = [a for f, m in PRESSES for a in ("--press", f"{f}:{m:x}")]
        live = run(self.rom, "--frames", "900", *press, "--record", log, "--hash")
        self.assertEqual(os.path.getsize(log), 5 + 3 * 900)
        self.assertEqual(run(self.rom, "--replay", log, "--hash"), live)
        self.assertNotEqual(run(self.rom, "--frames", "900", "--hash"), live)  # input mattered

    def test_file_format_written_independently(self):
        log = os.path.join(self.tmp, "py.nesi")
        write_log(log, 900, PRESSES)
        press = [a for f, m in PRESSES for a in ("--press", f"{f}:{m:x}")]
        self.assertEqual(run(self.rom, "--replay", log, "--hash"),
                         run(self.rom, "--frames", "900", *press, "--hash"))

    def test_reset_flag_is_replayed(self):
        plain, reset = os.path.join(self.tmp, "a.nesi"), os.path.join(self.tmp, "b.nesi")
        write_log(plain, 900, PRESSES)
        write_log(reset, 900, PRESSES, reset_at=850)
        h1, h2 = run(self.rom, "--replay", reset, "--hash"), run(self.rom, "--replay", reset, "--hash")
        self.assertEqual(h1, h2)
        self.assertNotEqual(h1, run(self.rom, "--replay", plain, "--hash"))


if __name__ == "__main__":
    unittest.main()
