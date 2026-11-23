"""Master System: the runtime self-checks, and a boot test on real games.

Needs the C build (-DTESTS=ON). The boot test needs the ROM set (.7z files)
in roms/Sega - Master System - Mark III or $MASTER_SYSTEM_ROMS_DIR, and 7z
on the PATH (or py7zr); it skips without them. Each sample game plays 900
frames with scripted input and must end on a picture of several colours,
not a blank screen (Micro Machines: the Codemasters mapper). The same for
Game Gear games, in roms/Sega - Game Gear or $GAME_GEAR_ROMS_DIR.
"""
import glob
import os
import re
import shutil
import subprocess
import tempfile
import unittest

from tests.games import ROOT, built

BUILD = os.environ.get("NES_BUILD", os.path.join(ROOT, "build"))
EXE = ".exe" if os.name == "nt" else ""
TESTS = os.path.join(BUILD, "sms_tests" + EXE)
RUNNER = os.path.join(BUILD, "sms_headless" + EXE)
SET = os.environ.get("MASTER_SYSTEM_ROMS_DIR", os.path.join(ROOT, "roms", "Sega - Master System - Mark III"))
SAMPLE = (
    "Alex Kidd in Miracle World (USA, Europe).7z",
    "Hang On (Europe).7z",
    "Phantasy Star (USA, Europe) (Rev A).7z",
    "Micro Machines (Europe).7z",
)
GG_SET = os.environ.get("GAME_GEAR_ROMS_DIR", os.path.join(ROOT, "roms", "Sega - Game Gear"))
GG_SAMPLE = (
    "Columns (USA, Europe, Brazil) (En).7z",
    "Ecco - The Tides of Time (USA, Europe, Brazil) (En).7z",
    "Defenders of Oasis (USA, Europe).7z",
    "Micro Machines (USA, Europe).7z",
)


def extract(archive, out):
    try:
        import py7zr
        with py7zr.SevenZipFile(archive) as z:
            z.extractall(path=out)
    except ImportError:
        subprocess.run(["7z", "x", "-y", "-o" + out, archive], capture_output=True, check=True)


@built(TESTS, "build with -DTESTS=ON")
class TestRuntime(unittest.TestCase):
    def test_runtime(self):
        r = subprocess.run([TESTS], capture_output=True, text=True)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)


def have_7z():
    try:
        import py7zr  # noqa: F401
        return True
    except ImportError:
        return shutil.which("7z") is not None


def boot(test, folder, sample):
    with tempfile.TemporaryDirectory() as d:
        for name in sample:
            path = os.path.join(folder, name)
            if not os.path.exists(path):
                continue  # another revision of the set
            with test.subTest(game=name):
                out = os.path.join(d, name[:-3])
                extract(path, out)
                files = [f for f in glob.glob(os.path.join(out, "**", "*"), recursive=True)
                         if f.lower().endswith((".sms", ".gg"))]  # a Game Gear set holds a few .sms
                rom = max(files, key=os.path.getsize)
                r = subprocess.run([RUNNER, rom, "--frames", "900", "--play"],
                                   capture_output=True, text=True, timeout=120)
                test.assertEqual(r.returncode, 0, r.stdout + r.stderr)
                m = re.search(r"colours=(\d+)", r.stdout)
                test.assertIsNotNone(m, r.stdout)
                test.assertGreaterEqual(int(m.group(1)), 4, r.stdout)


@built(RUNNER, "build the Master System headless program first")
@unittest.skipUnless(os.path.isdir(SET) and have_7z(), "no Master System set in " + SET + " or no 7z")
class TestBoot(unittest.TestCase):
    def test_sample_shows_a_picture(self):
        boot(self, SET, SAMPLE)


@built(RUNNER, "build the Master System headless program first")
@unittest.skipUnless(os.path.isdir(GG_SET) and have_7z(), "no Game Gear set in " + GG_SET + " or no 7z")
class TestGameGearBoot(unittest.TestCase):
    def test_sample_shows_a_picture(self):
        boot(self, GG_SET, GG_SAMPLE)


if __name__ == "__main__":
    unittest.main()
