"""A sample of the licensed Genesis set boots to its first VDP writes
(systems/genesis/docs/roadmap.md D1). Local only: needs the No-Intro set as .7z
files in roms/Sega - Mega Drive - Genesis/ (or $GENESIS_ROMS_DIR) and 7z;
skips without them. The sample: every 40th game, by name, leaving out
prototypes, betas, demos, pirate and unlicensed dumps, and BIOS images.
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
RUNNER = os.path.join(BUILD, "genesis_headless" + EXE)
SET = os.environ.get("GENESIS_ROMS_DIR", os.path.join(ROOT, "roms", "Sega - Mega Drive - Genesis"))
NOT_LICENSED = re.compile(r"\((Proto|Beta|Pirate|Unl|Demo|Program|Sample|Aftermarket|Homebrew|Pico|SegaNet|"
                          r"Sega Channel|Virtual Console|Test)")


def sample(root=SET, every=40):
    games = sorted(glob.glob(os.path.join(root, "**", "*.7z"), recursive=True))
    return [g for g in games if not NOT_LICENSED.search(os.path.basename(g))
            and not os.path.basename(g).startswith("[BIOS]")][::every]


@built(RUNNER, "build the C runtime first")
@unittest.skipUnless(os.path.isdir(SET) and shutil.which("7z"), "no Genesis set in " + SET + " or no 7z")
class TestBoot(unittest.TestCase):
    def test_sample_reaches_the_vdp(self):
        for game in sample():
            with self.subTest(game=os.path.basename(game)), tempfile.TemporaryDirectory() as d:
                subprocess.run(["7z", "x", "-y", "-o" + d, game], capture_output=True, check=True)
                rom = max((os.path.join(d, n) for n in os.listdir(d)), key=os.path.getsize)  # not S&K's lock-on part
                r = subprocess.run([RUNNER, rom, "--boot", "--frames", "300"], capture_output=True, text=True)
                self.assertEqual(r.returncode, 0, r.stdout + r.stderr)


if __name__ == "__main__":
    unittest.main()
