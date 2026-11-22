"""tools/ci_systems.py: which systems a change selects for CI."""
import os
import sys
import unittest

from tests.games import ROOT

sys.path.insert(0, os.path.join(ROOT, "tools"))
from ci_systems import systems_for  # noqa: E402


class TestCiSystems(unittest.TestCase):
    def test_own_folder_selects_one_system(self):
        self.assertEqual(systems_for(["gb/runtime/ppu.c", "tests/gb/test_ppu.py"]), ["gb"])
        self.assertEqual(systems_for(["nes/runtime/apu.c"]), ["nes"])

    def test_docs_select_nothing(self):
        self.assertEqual(systems_for(["README.md", "nes/README.md", "docs/roadmap.md", "dist/README.txt"]), [])

    def test_shared_code_selects_every_system(self):
        for f in ("core/frontend/sdl3_main.c", "CMakeLists.txt", "tests/fetch_test_roms.py", ".github/workflows/ci.yml"):
            self.assertEqual(systems_for([f, "gb/runtime/gb.c"]), ["nes", "gb", "genesis"], f)

    def test_cpu_folder_selects_its_systems(self):
        self.assertEqual(systems_for(["cpu/6502/c6502.c"]), ["nes"])
        self.assertEqual(systems_for(["cpu/sm83/sm83_ops.h", "gb/runtime/gb.c"]), ["gb"])
        self.assertEqual(systems_for(["cpu/6502/ops.c", "cpu/sm83/sm83.c"]), ["nes", "gb"])
        self.assertEqual(systems_for(["cpu/README.md"]), [])
        self.assertEqual(systems_for(["cpu/m68k/m68k.c"]), ["genesis"])

    def test_unknown_cpu_selects_every_system(self):
        for f in ("cpu/arm7/arm7.c", "cpu/CMakeLists.txt"):
            self.assertEqual(systems_for([f]), ["nes", "gb", "genesis"], f)

    def test_two_systems(self):
        self.assertEqual(systems_for(["gb/runtime/gb.c", "nes/runtime/nes.c"]), ["nes", "gb"])


if __name__ == "__main__":
    unittest.main()
