"""Recompiler checks (docs/02 verification ladder 4-5).

Blocks and suggestions: through nesrecomp (tests/nesrecomp.py).
nestest: the CPU state after every recompiled block must equal the line of
nestest.log with the same cycle count. Battle City (local ROM, built with
-DNES_GAMES=...): lockstep against the interpreter with zero fallbacks.
"""
import os
import re
import subprocess
import tomllib
import unittest

from tests.fetch_test_roms import fetch
from tests.games import ROOT
from tests.nesrecomp import BUILT, make_rom, recompile
from tests.test_cpu import BUILD, LOG_STATE

EXE = ".exe" if os.name == "nt" else ""
NESTEST_RECOMP = os.path.join(BUILD, "nestest_recomp_tests" + EXE)
BC_HEADLESS = os.path.join(BUILD, "battle_city_headless" + EXE)
TRANSFERS = ("JMP", "JSR", "RTS", "RTI", "BRK", "KIL", "BPL", "BMI", "BVC", "BVS", "BCC", "BCS", "BNE", "BEQ")


def suggested(code, misses):
    """banked entries nesrecomp derives from MISS lines, for code at $8000."""
    _, saved = recompile(make_rom(code, vectors=(0x8000,) * 3), misses=misses)
    return [(int(b), int(a, 16)) for b, a in re.findall(r"\[(\d+), 0x([0-9A-F]{4})\],", saved)]


@unittest.skipUnless(BUILT, "build nesrecomp first (cmake --build build)")
class TestBlocks(unittest.TestCase):
    def test_every_instruction_in_exactly_one_block(self):
        with open(os.path.join(ROOT, "tests", "data", "nestest.toml"), "rb") as f:
            prog, _ = recompile(fetch("nestest.nes"), entries=tomllib.load(f)["entries"])
        pcs = [pc for b in prog.blocks.values() for pc in b]
        self.assertEqual(sorted(pcs), sorted(prog.instrs))
        self.assertGreater(len(pcs), 5000)
        for b in prog.blocks.values():  # a control transfer can only be the last instruction
            for pc in b[:-1]:
                self.assertNotIn(prog.instrs[pc].mn, TRANSFERS)

    def test_suggest_keeps_only_chain_starts(self):
        # $8000: LDA #1 / BNE $8006 / NOP / NOP / $8006: RTS
        code = bytes([0xA9, 0x01, 0xD0, 0x02, 0xEA, 0xEA, 0x60])
        misses = "MISS $8000 1 bank 0\nMISS $8002 1 bank 0\nMISS $8006 1 bank 0\nMISS $0300 5 (RAM)\n"
        self.assertEqual(suggested(code, misses), [(0, 0x8000)])

    def test_suggest_finds_loops_entered_from_elsewhere(self):
        # $8000: BIT $2002 / BVC $8000  (entered from RAM code, e.g. an NMI handler)
        code = bytes([0x2C, 0x02, 0x20, 0x50, 0xFB])
        self.assertEqual(suggested(code, "MISS $8000 9 bank 0\nMISS $8003 9 bank 0\n"), [(0, 0x8000)])


@unittest.skipUnless(os.path.exists(NESTEST_RECOMP), "configure CMake after fetching nestest, then build")
class TestNestestRecompiled(unittest.TestCase):
    def test_block_boundaries_match_log(self):
        want = {}
        with open(fetch("nestest.log")) as f:
            for line in f:
                m = LOG_STATE.match(line)
                want[int(m.group(3))] = f"{m.group(1)} {m.group(2)} CYC:{m.group(3)}"
        last = max(want)
        out = subprocess.run([NESTEST_RECOMP, "nestest-recomp", fetch("nestest.nes"), str(last)],
                             capture_output=True, text=True, check=True).stdout.splitlines()
        self.assertGreater(len(out), 3000)
        self.assertGreaterEqual(int(out[-1].split("CYC:")[1]), last)
        for line in out:
            self.assertEqual(want.get(int(line.split("CYC:")[1])), line)


@unittest.skipUnless(os.path.exists(BC_HEADLESS), "build with -DNES_GAMES=<Battle City ROM>")
class TestBattleCityRecompiled(unittest.TestCase):
    def test_lockstep_with_interpreter_and_no_fallbacks(self):
        # the build embeds the ROM, so no ROM path is needed
        r = subprocess.run([BC_HEADLESS, "--play", "1", "--frames", "6000", "--diff", "--misses"],
                           capture_output=True, text=True, timeout=600)
        self.assertIn("DIFF OK: 6000 frames", r.stdout, r.stdout[-2000:])
        self.assertEqual(re.findall(r"^MISS .*", r.stdout, re.M), [])
        self.assertIn("interpreted=0 ", r.stdout)


if __name__ == "__main__":
    unittest.main()
