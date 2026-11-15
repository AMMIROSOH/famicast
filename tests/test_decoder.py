"""Decoder checks through the instructions nesrecomp emits (tests/nesrecomp.py)."""
import re
import unittest

from tests.fetch_test_roms import fetch
from tests.nesrecomp import BUILT, falls_through, make_rom, recompile, target

LOG_LINE = re.compile(r"^([0-9A-F]{4})  ((?:[0-9A-F]{2} ){1,3})\s*(\*?)([A-Z]{3})")


def parse_nestest_log(path):
    """[(pc, bytes, unofficial, mnemonic)] from Nintendulator's nestest.log."""
    rows = []
    with open(path) as f:
        for line in f:
            m = LOG_LINE.match(line)
            assert m, line
            rows.append((int(m.group(1), 16), bytes.fromhex(m.group(2)), m.group(3) == "*", m.group(4)))
    return rows


@unittest.skipUnless(BUILT, "build nesrecomp first (cmake --build build)")
class TestDecode(unittest.TestCase):
    def test_modes_and_targets(self):
        # $8000: LDA ($11),Y / JMP ($1234)   $8005: BNE $8005 / RTS
        rom = make_rom(bytes([0xB1, 0x11, 0x6C, 0x34, 0x12, 0xD0, 0xFE, 0x60]), vectors=(0x8000,) * 3)
        prog, _ = recompile(rom, entries=[0x8005])
        lda, jmp, bne = prog.instrs[0x8000], prog.instrs[0x8002], prog.instrs[0x8005]
        self.assertEqual((lda.mn, lda.arg, len(lda.raw)), ("LDA", "($11),Y", 2))
        self.assertEqual((jmp.arg, target(jmp), falls_through(jmp)), ("($1234)", None, False))
        self.assertEqual(target(bne), 0x8005)  # BNE to itself
        self.assertIn(0x8007, prog.instrs)     # its fall-through


@unittest.skipUnless(BUILT, "build nesrecomp first (cmake --build build)")
class TestNestest(unittest.TestCase):
    """Every ROM instruction Nintendulator executed must be found statically,
    with the same bytes and mnemonic, once the dynamic jumps are given as
    hints (the role game.toml plays per game). nestest exercises all the
    classic cases: RTS and RTI used as jumps, JMP ($0200) through a RAM
    pointer, and code in RAM at $0300 (left to the runtime interpreter)."""

    @classmethod
    def setUpClass(cls):
        cls.log = parse_nestest_log(fetch("nestest.log"))
        # Fixed point: decode, collect transfers the decoder could not predict,
        # feed their ROM targets back as entries, repeat.
        hints = set()
        while True:
            prog, _ = recompile(fetch("nestest.nes"), entries=[0xC000, *sorted(hints)])
            dynamic = []  # (from_pc, kind, to_pc)
            for (pc, *_), (nxt, *_) in zip(cls.log, cls.log[1:]):
                ins = prog.instrs.get(pc)
                if ins is None:
                    continue
                succ = {target(ins), (pc + len(ins.raw)) & 0xFFFF if falls_through(ins) else None}
                if nxt not in succ:
                    dynamic.append((pc, ins.mn + ("(ind)" if ins.arg.startswith("(") and ins.mn == "JMP" else ""), nxt))
            new = {to for _, _, to in dynamic if to >= 0x8000} - hints
            if not new:
                break
            hints |= new
        cls.prog, cls.dynamic = prog, dynamic

    def test_dynamic_transfers_are_only_rts_rti_jmp_ind(self):
        self.assertEqual({mn for _, mn, _ in self.dynamic}, {"RTS", "RTI", "JMP(ind)"})  # nestest uses all three

    def test_log_instructions_are_decoded_identically(self):
        missing, wrong = set(), []
        for pc, raw, unofficial, mn in self.log:
            if pc < 0x8000:
                continue  # RAM code: runtime fallback
            ins = self.prog.instrs.get(pc)
            if ins is None:
                missing.add(pc)
                continue
            got = (ins.raw, ins.unofficial, ins.mn)
            if got != (raw, unofficial, mn):
                wrong.append((f"${pc:04X}", got, (raw, unofficial, mn)))
        self.assertFalse(wrong, wrong[:10])
        self.assertFalse(missing, sorted(f"${p:04X}" for p in missing)[:20])


if __name__ == "__main__":
    unittest.main()
