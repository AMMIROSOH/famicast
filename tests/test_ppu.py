"""Whole-system test ROMs on the interpreter (docs/02, verification ladder 6).

Needs the C build (see tests/test_cpu.py). ROMs come from the pinned
nes-test-roms mirror (tests/fetch_test_roms.py).
"""
import os
import subprocess
import unittest

from tests.fetch_test_roms import NTR_FILES, fetch_ntr
from tests.test_cpu import BUILD

RUNNER = os.path.join(BUILD, "nes_headless.exe" if os.name == "nt" else "nes_headless")
PENDING = {  # path -> why it is not expected to pass yet
    "ppu_read_buffer/test_ppu_read_buffer.nes": "mapper 3 (CNROM), M7",
}


@unittest.skipUnless(os.path.exists(RUNNER), "build the C runtime first (see tests/test_cpu.py)")
class TestRoms(unittest.TestCase):
    def test_rom_suites(self):
        for path, kind in NTR_FILES.items():
            if path in PENDING:
                continue
            with self.subTest(rom=path):
                if kind == "6000":
                    r = subprocess.run([RUNNER, fetch_ntr(path), "--blargg"],
                                       capture_output=True, text=True, timeout=300)
                    self.assertEqual(r.returncode, 0, r.stdout)
                else:  # result code in zero page after the test finished (1 = pass)
                    r = subprocess.run([RUNNER, fetch_ntr(path), "--frames", "600", "--peek", kind],
                                       capture_output=True, text=True, timeout=300)
                    self.assertIn("PEEK $00%s = 01" % kind.upper(), r.stdout)


if __name__ == "__main__":
    unittest.main()
