"""Whole-system test ROMs (CPU, PPU, APU, interrupts) on the interpreter
(docs/02, verification ladder 6).

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
    "sprdma_and_dmc_dma/sprdma_and_dmc_dma.nes": "DMC DMA during OAM DMA not modelled (shortcut in nes.c)",
    "sprdma_and_dmc_dma/sprdma_and_dmc_dma_512.nes": "DMC DMA during OAM DMA not modelled (shortcut in nes.c)",
    "apu_reset/4017_written.nes": "reset case #3 unclear without test source; power case passes (docs/06)",
    "apu_reset/len_ctrs_enabled.nes": "reset case #3 unclear without test source; power case passes (docs/06)",
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
