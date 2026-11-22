"""The 68000 recompiler (genesis/recomp) against the interpreter, in
lockstep: genesis_headless --diff compares the CPUs, the Z80 and the RAMs
after every line and the whole machine after every frame.

CI: a ROM of random instructions made at build time
(genesis/tests/random_rom.c, mdtest_random). Locally, the games recompiled
through -DROMS can be diffed the same way (NAME_headless ROM --diff).
"""
import os
import re
import subprocess
import unittest

from tests.games import ROOT, built

BUILD = os.environ.get("NES_BUILD", os.path.join(ROOT, "build"))
EXE = ".exe" if os.name == "nt" else ""
RUNNER = os.path.join(BUILD, "mdtest_random_headless" + EXE)
ROM = os.path.join(BUILD, "gen", "mdtest_random.md")


@built(RUNNER, "build the C runtime first")
class TestRecompiledRandom(unittest.TestCase):
    def test_lockstep(self):
        r = subprocess.run([RUNNER, ROM, "--frames", "120", "--diff", "--coverage"], capture_output=True, text=True)
        self.assertIn("diff: 120 frames, no difference", r.stdout, r.stdout + r.stderr)
        share = float(re.search(r"coverage: ([\d.]+)%", r.stdout).group(1))
        self.assertGreater(share, 90, r.stdout)


if __name__ == "__main__":
    unittest.main()
