"""Genesis Z80 interpreter vs SingleStepTests z80 (cpu/z80/z80_tests.c).

Needs the C build (z80_tests) and the tests: python tests/fetch_test_roms.py
--sst-z80 (files not downloaded are skipped; set GENESIS_SST_REQUIRED=1 to
fail instead). Every opcode is checked on registers (WZ, R, IFF, the shadow
set, Q), RAM, port accesses and each T-state's bus state.
"""
import os
import subprocess
import unittest

from tests.fetch_test_roms import ROMS, Z80_FILES

from tests.games import ROOT  # noqa: E402
from tests.games import built
BUILD = os.environ.get("NES_BUILD", os.path.join(ROOT, "build"))
EXE = ".exe" if os.name == "nt" else ""
RUNNER = os.path.join(BUILD, "z80_tests" + EXE)
MAGIC = b"Z80S\x01"  # tests/fetch_test_roms.py: files of the first converter (no cycles) have none


@built(RUNNER, "build the C runtime first")
class TestSingleStepZ80(unittest.TestCase):
    def test_every_opcode(self):
        required = os.environ.get("GENESIS_SST_REQUIRED") == "1"
        for name in Z80_FILES:
            path = os.path.join(ROMS, "sst_z80", name.replace(" ", "_") + ".bin")
            with self.subTest(op=name):
                if not os.path.exists(path):
                    if required:
                        self.fail("missing " + path)
                    continue
                with open(path, "rb") as f:
                    if f.read(len(MAGIC)) != MAGIC:
                        self.fail(path + " is from the old converter: python tests/fetch_test_roms.py --sst-z80")
                r = subprocess.run([RUNNER, "sst", path], capture_output=True, text=True)
                self.assertEqual(r.returncode, 0, r.stdout + r.stderr)


@built(RUNNER, "build the C runtime first")
class TestInterrupts(unittest.TestCase):
    """What the corpus does not cover: HALT, EI's delay, IM 0/1/2 and NMI (z80_tests self)."""
    def test_self(self):
        r = subprocess.run([RUNNER, "self"], capture_output=True, text=True)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)


if __name__ == "__main__":
    unittest.main()
