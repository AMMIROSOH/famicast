"""Master System: the runtime self-checks, and a boot test on real games.

Needs the C build (-DTESTS=ON). The boot test needs the ROM set (.7z files)
in roms/Sega - Master System - Mark III or $MASTER_SYSTEM_ROMS_DIR, and 7z
on the PATH (or py7zr); it skips without them. Each sample game plays 900
frames with scripted input and must end on a picture of several colours,
not a blank screen (Micro Machines: the Codemasters mapper).
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


@built(RUNNER, "build the Master System headless program first")
@unittest.skipUnless(os.path.isdir(SET) and have_7z(), "no Master System set in " + SET + " or no 7z")
class TestBoot(unittest.TestCase):
    def test_sample_shows_a_picture(self):
        with tempfile.TemporaryDirectory() as d:
            for name in SAMPLE:
                path = os.path.join(SET, name)
                if not os.path.exists(path):
                    continue  # another revision of the set
                with self.subTest(game=name):
                    out = os.path.join(d, name[:-3])
                    extract(path, out)
                    rom = max(glob.glob(os.path.join(out, "**", "*.sms"), recursive=True), key=os.path.getsize)
                    r = subprocess.run([RUNNER, rom, "--frames", "900", "--play"],
                                       capture_output=True, text=True, timeout=120)
                    self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
                    m = re.search(r"colours=(\d+)", r.stdout)
                    self.assertIsNotNone(m, r.stdout)
                    self.assertGreaterEqual(int(m.group(1)), 4, r.stdout)


if __name__ == "__main__":
    unittest.main()
