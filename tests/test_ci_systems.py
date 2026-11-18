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
            self.assertEqual(systems_for([f, "gb/runtime/gb.c"]), ["nes", "gb"], f)

    def test_two_systems(self):
        self.assertEqual(systems_for(["gb/runtime/gb.c", "nes/runtime/nes.c"]), ["nes", "gb"])


if __name__ == "__main__":
    unittest.main()
