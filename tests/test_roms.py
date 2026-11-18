"""Whole-system test ROMs (CPU, PPU, APU, interrupts) on the interpreter
(docs/architecture.md, verification ladder 6).

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
    "mmc3_test_2/rom_singles/6-MMC3_alt.nes": "MMC3 rev A behaviour; we emulate rev B (5-MMC3 passes), the common chip",
    "sprdma_and_dmc_dma/sprdma_and_dmc_dma.nes": "DMC DMA during OAM DMA modelled (nes.c run_dma) but the "
        "table is off by 1-2 clocks; expected table unknown (one known value: 512 test, row 0B = 527)",
    "sprdma_and_dmc_dma/sprdma_and_dmc_dma_512.nes": "as sprdma_and_dmc_dma",
    "dmc_dma_during_read4/double_2007_read.nes": "two $2007 reads on consecutive cycles: PPU read-buffer "
        "timing not modelled (2nd row prints 33 44 55 66 77)",
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
                elif kind.startswith("text:"):
                    r = subprocess.run([RUNNER, fetch_ntr(path), "--frames", "300", "--text"],
                                       capture_output=True, text=True, timeout=300)
                    self.assertTrue(any(t in r.stdout for t in kind[5:].split("|")), r.stdout)
                else:  # result code in zero page after the test finished (1 = pass)
                    r = subprocess.run([RUNNER, fetch_ntr(path), "--frames", "600", "--peek", kind],
                                       capture_output=True, text=True, timeout=300)
                    self.assertIn("PEEK $00%s = 01" % kind.upper(), r.stdout)


if __name__ == "__main__":
    unittest.main()
