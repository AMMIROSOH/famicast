"""CPS-1: the runtime self-checks, and Final Fight reaching its title.

Needs the C build (-DTESTS=ON). The game test needs the CPS-1 set (.zip
files) in roms/CAPCOM - Play System I or $CPS1_ROMS_DIR; it skips without
it. Final Fight plays 1,200 frames, a coin and Start tapped, and must end
on a picture of many colours, not a blank screen.
"""
import os
import re
import subprocess
import tempfile
import unittest
import zipfile

from tests.games import ROOT, built

BUILD = os.environ.get("NES_BUILD", os.path.join(ROOT, "build"))
EXE = ".exe" if os.name == "nt" else ""
TESTS = os.path.join(BUILD, "cps1_tests" + EXE)
RUNNER = os.path.join(BUILD, "cps1_headless" + EXE)
SET = os.environ.get("CPS1_ROMS_DIR", os.path.join(ROOT, "roms", "CAPCOM - Play System I"))


@built(TESTS, "build the CPS-1 self-checks first")
class TestRuntime(unittest.TestCase):
    def test_self_checks(self):
        r = subprocess.run([TESTS], capture_output=True, text=True, timeout=60)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)


@built(RUNNER, "build the CPS-1 headless program first")
class TestHeadless(unittest.TestCase):
    def test_unknown_zip(self):
        with tempfile.TemporaryDirectory() as d:
            path = os.path.join(d, "x.zip")
            with zipfile.ZipFile(path, "w") as z:
                z.writestr("x.bin", os.urandom(4096))
            r = subprocess.run([RUNNER, path, "--frames", "1"], capture_output=True, text=True, timeout=60)
            self.assertEqual(r.returncode, 1)
            self.assertIn("not a CPS-1 game", r.stdout + r.stderr)

    @unittest.skipUnless(os.path.exists(os.path.join(SET, "ffight.zip")), "no ffight.zip in " + SET)
    def test_ffight_title(self):
        with tempfile.TemporaryDirectory() as d:
            ppm = os.path.join(d, "out.ppm")
            r = subprocess.run([RUNNER, os.path.join(SET, "ffight.zip"), "--frames", "1200", "--play", "--ppm", ppm],
                               capture_output=True, text=True, timeout=300)
            self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
            m = re.search(r"colours=(\d+)", r.stdout)
            self.assertIsNotNone(m, r.stdout)
            self.assertGreaterEqual(int(m.group(1)), 16, r.stdout)
            self.assertTrue(os.path.getsize(ppm) > 384 * 224 * 3)


if __name__ == "__main__":
    unittest.main()
