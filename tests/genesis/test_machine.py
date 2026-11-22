"""The Genesis machine's self-checks (systems/genesis/tests/md_tests.c): version
register, work RAM, Z80 reset, bus request and bank, its vertical blank
interrupt, 3- and 6-button pads, battery RAM, TMSS. They check the code
against the hardware facts in systems/genesis/docs/roadmap.md, not a console.
"""
import os
import subprocess
import unittest

from tests.games import ROOT, built

BUILD = os.environ.get("NES_BUILD", os.path.join(ROOT, "build"))
EXE = ".exe" if os.name == "nt" else ""
RUNNER = os.path.join(BUILD, "md_tests" + EXE)


@built(RUNNER, "build the C runtime first")
class TestMachine(unittest.TestCase):
    def test_self_checks(self):
        r = subprocess.run([RUNNER], capture_output=True, text=True)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)


if __name__ == "__main__":
    unittest.main()
