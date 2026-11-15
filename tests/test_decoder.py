import re
import unittest

from recomp.decoder import decode_at, explore
from recomp.ines import Cartridge
from tests.fetch_test_roms import fetch

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


class TestDecodeAt(unittest.TestCase):
    def test_modes_and_targets(self):
        mem = bytearray(0x10000)
        mem[0x8000:0x8008] = bytes([0xB1, 0x11, 0x6C, 0x34, 0x12, 0xD0, 0xFE, 0x60])
        lda = decode_at(mem, 0x8000)
        self.assertEqual((lda.op.mn, lda.op.mode, lda.size, lda.operand), ("LDA", "IZY", 2, 0x11))
        jmp = decode_at(mem, 0x8002)
        self.assertEqual((jmp.op.mode, jmp.operand, jmp.target), ("IND", 0x1234, None))
        self.assertEqual(decode_at(mem, 0x8005).target, 0x8005)  # BNE to itself


class TestNestest(unittest.TestCase):
    """Every ROM instruction Nintendulator executed must be found statically,
    with the same bytes and mnemonic, once the dynamic jumps are given as
    hints (the role game.toml plays per game). nestest exercises all the
    classic cases: RTS and RTI used as jumps, JMP ($0200) through a RAM
    pointer, and code in RAM at $0300 (left to the runtime interpreter)."""

    @classmethod
    def setUpClass(cls):
        cls.mem = Cartridge.load(fetch("nestest.nes")).nrom_image()
        cls.log = parse_nestest_log(fetch("nestest.log"))
        # Fixed point: decode, collect transfers the decoder could not predict,
        # feed their ROM targets back as entries, repeat.
        hints = set()
        while True:
            prog = explore(cls.mem, [0xC000, *hints])
            dynamic = []  # (from_pc, kind, to_pc)
            for (pc, *_), (nxt, *_) in zip(cls.log, cls.log[1:]):
                ins = prog.instrs.get(pc)
                if ins is None:
                    continue
                succ = {ins.target, (pc + ins.size) & 0xFFFF if ins.falls_through else None}
                if nxt not in succ:
                    dynamic.append((pc, ins.op.mn + ("(ind)" if ins.op.mode == "IND" else ""), nxt))
            new = {to for _, _, to in dynamic if to >= 0x8000} - hints
            if not new:
                break
            hints |= new
        cls.prog, cls.dynamic = prog, dynamic

    def test_dynamic_transfers_are_only_rts_rti_jmp_ind(self):
        kinds = {mn for _, mn, _ in self.dynamic}
        self.assertLessEqual(kinds, {"RTS", "RTI", "JMP(ind)"})
        self.assertEqual(kinds, {"RTS", "RTI", "JMP(ind)"})  # nestest uses all three

    def test_log_instructions_are_decoded_identically(self):
        missing, wrong = set(), []
        for pc, raw, unofficial, mn in self.log:
            if pc < 0x8000:
                continue  # RAM code: runtime fallback

            ins = self.prog.instrs.get(pc)
            if ins is None:
                missing.add(pc)
                continue
            got = (bytes(self.mem[pc:pc + ins.size]), not ins.op.official, ins.op.mn)
            if got != (raw, unofficial, mn):
                wrong.append((f"${pc:04X}", got, (raw, unofficial, mn)))
        self.assertFalse(wrong, wrong[:10])
        self.assertFalse(missing, sorted(f"${p:04X}" for p in missing)[:20])


if __name__ == "__main__":
    unittest.main()
