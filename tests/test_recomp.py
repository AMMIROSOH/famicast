"""Recompiler checks (docs/02 verification ladder 4-5).

nestest: the CPU state after every recompiled block must equal the line of
nestest.log with the same cycle count. Battle City (local ROM, built with
-DNES_GAMES=...): lockstep against the interpreter with zero fallbacks.
"""
import os
import re
import subprocess
import tempfile
import tomllib
import unittest

from recomp.blocks import split_blocks
from recomp.decoder import explore
from recomp.ines import Cartridge
from recomp.suggest import chain_starts
from tests import play_script
from tests.fetch_test_roms import fetch
from tests.games import BATTLE_CITY, ROOT, find_rom
from tests.test_cpu import BUILD, LOG_STATE

EXE = ".exe" if os.name == "nt" else ""
NESTEST_RECOMP = os.path.join(BUILD, "nestest_recomp_tests" + EXE)
BC_HEADLESS = os.path.join(BUILD, "battle_city_headless" + EXE)


class TestBlocks(unittest.TestCase):
    def test_every_instruction_in_exactly_one_block(self):
        mem = Cartridge.load(fetch("nestest.nes")).nrom_image()
        with open(os.path.join(ROOT, "tests", "data", "nestest.toml"), "rb") as f:
            prog = explore(mem, tomllib.load(f)["entries"])
        blocks = split_blocks(prog)
        pcs = [i.pc for b in blocks for i in b.instrs]
        self.assertEqual(sorted(pcs), sorted(prog.instrs))
        for b in blocks:  # a control transfer can only be the last instruction
            for ins in b.instrs[:-1]:
                self.assertNotIn(ins.op.mn, ("JMP", "JSR", "RTS", "RTI", "BRK", "BNE", "BEQ"))

    def test_suggest_keeps_only_chain_starts(self):
        mem = bytearray(0x10000)
        # $8000: LDA #1 / BNE $8006 / NOP / NOP / $8006: RTS
        mem[0x8000:0x8007] = bytes([0xA9, 0x01, 0xD0, 0x02, 0xEA, 0xEA, 0x60])
        self.assertEqual(chain_starts(mem, {0x8000, 0x8002, 0x8006}), [0x8000])

    def test_suggest_finds_loops_entered_from_elsewhere(self):
        mem = bytearray(0x10000)
        # $8000: BIT $2002 / BVC $8000  (entered from RAM code, e.g. an NMI handler)
        mem[0x8000:0x8005] = bytes([0x2C, 0x02, 0x20, 0x50, 0xFB])
        self.assertEqual(chain_starts(mem, {0x8000, 0x8003}), [0x8000])


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
        rom = find_rom(BATTLE_CITY)
        if not rom:
            self.skipTest("Battle City ROM not found (set NES_GAMES_DIR)")
        with tempfile.TemporaryDirectory() as tmp:
            rec = os.path.join(tmp, "play.nesi")
            play_script.write(rec, play_script.frames(6000, seed=1))
            r = subprocess.run([BC_HEADLESS, rom, "--replay", rec, "--diff", "--misses"],
                               capture_output=True, text=True, timeout=600)
        self.assertIn("DIFF OK: 6000 frames", r.stdout, r.stdout[-2000:])
        self.assertEqual(re.findall(r"^MISS .*", r.stdout, re.M), [])
        self.assertIn("interpreted=0 ", r.stdout)


if __name__ == "__main__":
    unittest.main()
