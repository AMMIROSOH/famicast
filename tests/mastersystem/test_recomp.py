"""The Master System Z80 recompiler against the interpreter, in lockstep.

CI: a ROM of random legal Z80 instructions made at build time
(systems/mastersystem/tests/random_rom.c, smstest_random: every opcode
table but DD CB / FD CB, memory writes, conditional branches), all of it
recompiled, 600 frames in lockstep with the interpreter. Locally, games
recompiled through -DROMS can be diffed the same way:
  build/NAME_headless game.sms --diff --coverage
"""
import os
import re
import subprocess
import unittest

from tests.games import ROOT, built

BUILD = os.environ.get("NES_BUILD", os.path.join(ROOT, "build"))
EXE = ".exe" if os.name == "nt" else ""
RUNNER = os.path.join(BUILD, "smstest_random_headless" + EXE)
ROM = os.path.join(BUILD, "gen", "smstest_random.sms")


@built(RUNNER, "build the Master System recompiler and its test ROM first")
class TestRecompiledRandom(unittest.TestCase):
    def test_lockstep(self):
        r = subprocess.run([RUNNER, ROM, "--frames", "600", "--diff", "--coverage"],
                           capture_output=True, text=True, timeout=180)
        self.assertIn("diff: 600 frames, no difference", r.stdout, r.stdout + r.stderr)
        share = float(re.search(r"coverage: ([\d.]+)%", r.stdout).group(1))
        self.assertEqual(share, 100.0, r.stdout)
