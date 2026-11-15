import json
import os
import unittest

from tests.nesrecomp import BUILT, ops

DATA = os.path.join(os.path.dirname(__file__), "data", "r2_6502.json")

# radare2 names for unofficial opcodes -> our (nesdev/nestest) names
R2_ALIAS = {"ane": "XAA", "asr": "ALR", "hlt": "KIL", "lae": "LAS",
            "lxa": "LAX", "sbx": "AXS", "sha": "AHX", "shs": "TAS"}

# Where we deliberately disagree with radare2. Each entry: opcode -> reason.
# If radare2 is fixed or our table changes, test_matches_radare2 fails and this
# list must be revisited.
R2_KNOWN_WRONG = {
    0x93: "radare2 lists SHA $93 as 'abs,X', 3 bytes; it is (zp),Y, 2 bytes "
          "(nesdev 'CPU unofficial opcodes', AHX/SHA)",
    0xCB: "radare2 prints AXS/SBX $CB without '#'; it is immediate (same size)",
}


@unittest.skipUnless(BUILT, "build nesrecomp first (cmake --build build)")
class TestOps(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        global OPS
        OPS = ops()

    def test_256_entries_and_151_official(self):
        self.assertEqual([o.code for o in OPS], list(range(256)))
        self.assertEqual(sum(o.official for o in OPS), 151)

    def test_matches_radare2(self):
        with open(DATA) as f:
            ref = json.load(f)["ops"]
        mismatches = {}
        for o in OPS:
            r = ref["%02X" % o.code]
            mn = R2_ALIAS.get(r["mn"], r["mn"].upper())
            if (mn, r["mode"], r["size"]) != (o.mn, o.mode, o.size):
                mismatches[o.code] = (o, r)
        self.assertEqual(sorted(mismatches), sorted(R2_KNOWN_WRONG),
                         "\n".join(f"${c:02X}: ours {o} vs r2 {r}" for c, (o, r) in
                                   sorted(mismatches.items()) if c not in R2_KNOWN_WRONG))

    def test_regressions_from_prototype(self):
        # docs/06: (zp),Y was decoded as 3 bytes; JMP (abs) shares nothing with it
        self.assertEqual((OPS[0xB1].mode, OPS[0xB1].size), ("IZY", 2))
        self.assertEqual((OPS[0x6C].mode, OPS[0x6C].size), ("IND", 3))
        self.assertEqual(OPS[0x0A].mn, "ASL")  # no PHX on the 2A03
        self.assertEqual(OPS[0x9D].cycles, 5)  # STA abs,X: 5 (prototype said 4)

    def test_page_penalty(self):
        self.assertTrue(OPS[0xBD].page_penalty)       # LDA abs,X
        self.assertFalse(OPS[0x9D].page_penalty)      # STA abs,X
        self.assertFalse(OPS[0xFE].page_penalty)      # INC abs,X (RMW)


if __name__ == "__main__":
    unittest.main()
