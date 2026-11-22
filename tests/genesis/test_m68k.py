"""68000 interpreter vs SingleStepTests m68000 (cpu/m68k/m68k_tests.c).

Needs the C build (m68k_tests) and the tests: python tests/fetch_test_roms.py
--sst-m68k (files not downloaded are skipped; GENESIS_SST_REQUIRED=1 fails
instead). Every family is checked on registers, SR, PC, the prefetch queue,
RAM and every bus transaction with its clocks; TAS and TRAPV on state only
(the corpus' README: their transaction logs are not reliable). Every family
is expected to pass all 2,500 of its tests.
"""
import os
import subprocess
import unittest

from tests.fetch_test_roms import M68K_FILES, ROMS

from tests.games import ROOT  # noqa: E402
from tests.games import built

BUILD = os.environ.get("NES_BUILD", os.path.join(ROOT, "build"))
EXE = ".exe" if os.name == "nt" else ""
RUNNER = os.path.join(BUILD, "m68k_tests" + EXE)
END_ONLY = {"TAS", "TRAPV"}


@built(RUNNER, "build the C runtime first")
class TestSingleStepM68k(unittest.TestCase):
    def test_every_family(self):
        required = os.environ.get("GENESIS_SST_REQUIRED") == "1"
        for name in M68K_FILES:
            path = os.path.join(ROMS, "sst_m68000", name + ".bin")
            with self.subTest(family=name):
                if not os.path.exists(path):
                    if required:
                        self.fail("missing " + path)
                    continue
                mode = "sst-end" if name in END_ONLY else "sst"
                r = subprocess.run([RUNNER, mode, path], capture_output=True, text=True)
                self.assertEqual(r.returncode, 0, r.stdout + r.stderr)


if __name__ == "__main__":
    unittest.main()
