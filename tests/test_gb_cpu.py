"""Game Boy SM83 interpreter vs SingleStepTests sm83 (gb/tests/cpu_tests.c).

Needs the C build (gb_cpu_tests) and the tests: python tests/fetch_test_roms.py
--sst-sm83 (files not downloaded are skipped; set GB_SST_REQUIRED=1 to fail
instead). Every opcode is checked on registers, RAM and each M-cycle's bus
state; HALT and STOP only on registers and RAM (what they do next is the
system's business).
"""
import os
import subprocess
import unittest

from tests.fetch_test_roms import ROMS, SM83_FILES

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUILD = os.environ.get("NES_BUILD", os.path.join(ROOT, "build"))
EXE = ".exe" if os.name == "nt" else ""
RUNNER = os.path.join(BUILD, "gb_cpu_tests" + EXE)
GBRECOMP = os.path.join(BUILD, "gbrecomp" + EXE)
END_ONLY = {"76", "10"}  # HALT, STOP


@unittest.skipUnless(os.path.exists(RUNNER), "build the C runtime first")
class TestSingleStepSm83(unittest.TestCase):
    def test_every_opcode(self):
        required = os.environ.get("GB_SST_REQUIRED") == "1"
        for name in SM83_FILES:
            path = os.path.join(ROMS, "sst_sm83", name.replace(" ", "_") + ".bin")
            with self.subTest(op=name):
                if not os.path.exists(path):
                    if required:
                        self.fail("missing " + path)
                    continue
                mode = "sst-end" if name in END_ONLY else "sst"
                r = subprocess.run([RUNNER, mode, path], capture_output=True, text=True)
                self.assertEqual(r.returncode, 0, r.stdout + r.stderr)


@unittest.skipUnless(os.path.exists(GBRECOMP), "build gbrecomp first")
class TestOpcodeTable(unittest.TestCase):
    def test_sizes_and_names(self):
        lines = subprocess.run([GBRECOMP, "--ops"], capture_output=True, text=True, check=True).stdout.splitlines()
        ops = {line.split(" ", 1)[0]: line.split(" ", 2)[1:] for line in lines}
        self.assertEqual(len(ops), 512)
        # spot checks against the Pan Docs opcode table
        for code, size, name in [("00", "1", "NOP"), ("01", "3", "LD BC,nn"), ("08", "3", "LD (nn),SP"),
                                 ("18", "2", "JR e"), ("22", "1", "LD (HL+),A"), ("36", "2", "LD (HL),n"),
                                 ("76", "1", "HALT"), ("C3", "3", "JP nn"), ("CD", "3", "CALL nn"),
                                 ("E0", "2", "LDH (n),A"), ("E8", "2", "ADD SP,e"), ("F8", "2", "LD HL,SP+e"),
                                 ("CB37", "2", "SWAP A"), ("CB7E", "2", "BIT 7,(HL)"), ("CBC6", "2", "SET 0,(HL)")]:
            self.assertEqual(ops[code], [size, name], code)


if __name__ == "__main__":
    unittest.main()
