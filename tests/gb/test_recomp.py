"""Game Boy recompiler (gb/recomp): the blocks gbrecomp makes from a small
made-up ROM, and blargg's cpu_instrs recompiled (gb/CMakeLists.txt
gbtest_cpu_instrs): it must pass and match the interpreter in lockstep."""
import os
import re
import subprocess
import tempfile
import unittest

from tests.fetch_test_roms import fetch_gb_test_roms
from tests.gb.test_roms import BUILD
from tests.games import built

EXE = ".exe" if os.name == "nt" else ""
GBRECOMP = os.path.join(BUILD, "gbrecomp" + EXE)
CPU_INSTRS = os.path.join(BUILD, "gbtest_cpu_instrs_headless" + EXE)


def make_rom():
    """64 KB, MBC1. Vectors: RET. $0100: NOP, JP $0150.
    $0150 LD A,2 / $0152 LD ($2000),A / $0155 CALL $4000 / $0158 JR NZ,$0155 / $015A JR $015A
    bank 1, $4000: INC A / RET"""
    rom = bytearray(0x10000)
    rom[0:0x100] = b"\xC9" * 0x100
    rom[0x100:0x104] = bytes([0x00, 0xC3, 0x50, 0x01])
    rom[0x147], rom[0x148] = 0x01, 0x01
    rom[0x150:0x15C] = bytes([0x3E, 0x02, 0xEA, 0x00, 0x20, 0xCD, 0x00, 0x40, 0x20, 0xFB, 0x18, 0xFE])
    rom[0x4000:0x4002] = bytes([0x3C, 0xC9])
    return bytes(rom)


def blocks(c_source):
    """{block name: its C}"""
    parts = re.split(r"^static void (b_\w+)\(GbCpu \*c\) \{$", c_source, flags=re.M)
    return {parts[i]: parts[i + 1].split("\n}\n", 1)[0] for i in range(1, len(parts), 2)}


@built(GBRECOMP, "build gbrecomp first")
class TestBlocks(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        with tempfile.TemporaryDirectory() as tmp:
            rom, trace, out = (os.path.join(tmp, n) for n in ("t.gb", "t.trace", "t.c"))
            with open(rom, "wb") as f:
                f.write(make_rom())
            with open(trace, "w") as f:
                f.write("MISS $4000 bank 1\n")
            subprocess.run([GBRECOMP, rom, "-o", out, "--misses", trace], check=True, capture_output=True)
            with open(out) as f:
                cls.c = f.read()
        cls.b = blocks(cls.c)

    def test_block_starts(self):
        mine = sorted(n for n in self.b if n.startswith("b_000_01"))
        # $0155 starts a block (JR target), so $0150's block ends before it; after CALL, $0158
        self.assertEqual(mine, ["b_000_0100", "b_000_0150", "b_000_0155", "b_000_0158", "b_000_015A"])

    def test_bank_switch_checked(self):
        self.assertIn("if (gb_cart_bank(&g->cart, 0x0000) != 0) return;", self.b["b_000_0150"])

    def test_chaining_stays_in_the_window(self):
        self.assertIn("RECOMP_CHAIN(b_000_0155)", self.b["b_000_0150"])  # falls into it
        self.assertNotIn("RECOMP_CHAIN", self.b["b_000_0155"])           # CALL $4000: bank unknown here
        for t in ("b_000_0155", "b_000_015A"):
            self.assertIn("RECOMP_CHAIN(%s)" % t, self.b["b_000_0158"])

    def test_banked_entry_from_trace(self):
        self.assertIn("b_001_4000", self.b)
        self.assertIn("[1][1] = r_001_1", self.c)
        self.assertNotIn("[1][0]", self.c)


@built(CPU_INSTRS, "fetch the test ROMs, configure CMake, then build")
class TestCpuInstrsRecompiled(unittest.TestCase):
    ROM = os.path.join(fetch_gb_test_roms(), "blargg", "cpu_instrs", "cpu_instrs.gb") if os.path.exists(CPU_INSTRS) else ""

    def test_passes(self):
        r = subprocess.run([CPU_INSTRS, self.ROM, "--frames", "4000", "--coverage"], capture_output=True, text=True)
        self.assertIn("result: passed", r.stdout)
        self.assertRegex(r.stdout, r"COVERAGE [1-9]")  # some of it ran recompiled (most runs from RAM)

    def test_lockstep_with_the_interpreter(self):
        r = subprocess.run([CPU_INSTRS, self.ROM, "--frames", "4000", "--diff"], capture_output=True, text=True)
        self.assertIn("DIFF OK", r.stdout, r.stdout[-1500:])


if __name__ == "__main__":
    unittest.main()
