"""2A03 (NES 6502) opcode table: all 256 opcodes.

Laid out as 16x16 grids (row = high nibble, column = low nibble), the same
shape as the nesdev wiki "CPU unofficial opcodes" matrix, so it can be checked
by eye. Checked by tests/test_ops.py against radare2's tables
(tests/data/r2_6502.json); cycle counts are checked later against nestest.log
and the SingleStepTests suite (docs/05, M2).

Unofficial names follow nesdev/nestest: SLO RLA SRE RRA SAX LAX DCP ISB ANC
ALR ARR XAA AXS AHX SHY SHX TAS LAS KIL.
"""

MNEMONICS = """
BRK ORA KIL SLO NOP ORA ASL SLO PHP ORA ASL ANC NOP ORA ASL SLO
BPL ORA KIL SLO NOP ORA ASL SLO CLC ORA NOP SLO NOP ORA ASL SLO
JSR AND KIL RLA BIT AND ROL RLA PLP AND ROL ANC BIT AND ROL RLA
BMI AND KIL RLA NOP AND ROL RLA SEC AND NOP RLA NOP AND ROL RLA
RTI EOR KIL SRE NOP EOR LSR SRE PHA EOR LSR ALR JMP EOR LSR SRE
BVC EOR KIL SRE NOP EOR LSR SRE CLI EOR NOP SRE NOP EOR LSR SRE
RTS ADC KIL RRA NOP ADC ROR RRA PLA ADC ROR ARR JMP ADC ROR RRA
BVS ADC KIL RRA NOP ADC ROR RRA SEI ADC NOP RRA NOP ADC ROR RRA
NOP STA NOP SAX STY STA STX SAX DEY NOP TXA XAA STY STA STX SAX
BCC STA KIL AHX STY STA STX SAX TYA STA TXS TAS SHY STA SHX AHX
LDY LDA LDX LAX LDY LDA LDX LAX TAY LDA TAX LAX LDY LDA LDX LAX
BCS LDA KIL LAX LDY LDA LDX LAX CLV LDA TSX LAS LDY LDA LDX LAX
CPY CMP NOP DCP CPY CMP DEC DCP INY CMP DEX AXS CPY CMP DEC DCP
BNE CMP KIL DCP NOP CMP DEC DCP CLD CMP NOP DCP NOP CMP DEC DCP
CPX SBC NOP ISB CPX SBC INC ISB INX SBC NOP SBC CPX SBC INC ISB
BEQ SBC KIL ISB NOP SBC INC ISB SED SBC NOP ISB NOP SBC INC ISB
"""

MODES = """
IMP IZX IMP IZX ZP  ZP  ZP  ZP  IMP IMM ACC IMM ABS ABS ABS ABS
REL IZY IMP IZY ZPX ZPX ZPX ZPX IMP ABY IMP ABY ABX ABX ABX ABX
ABS IZX IMP IZX ZP  ZP  ZP  ZP  IMP IMM ACC IMM ABS ABS ABS ABS
REL IZY IMP IZY ZPX ZPX ZPX ZPX IMP ABY IMP ABY ABX ABX ABX ABX
IMP IZX IMP IZX ZP  ZP  ZP  ZP  IMP IMM ACC IMM ABS ABS ABS ABS
REL IZY IMP IZY ZPX ZPX ZPX ZPX IMP ABY IMP ABY ABX ABX ABX ABX
IMP IZX IMP IZX ZP  ZP  ZP  ZP  IMP IMM ACC IMM IND ABS ABS ABS
REL IZY IMP IZY ZPX ZPX ZPX ZPX IMP ABY IMP ABY ABX ABX ABX ABX
IMM IZX IMM IZX ZP  ZP  ZP  ZP  IMP IMM IMP IMM ABS ABS ABS ABS
REL IZY IMP IZY ZPX ZPX ZPY ZPY IMP ABY IMP ABY ABX ABX ABY ABY
IMM IZX IMM IZX ZP  ZP  ZP  ZP  IMP IMM IMP IMM ABS ABS ABS ABS
REL IZY IMP IZY ZPX ZPX ZPY ZPY IMP ABY IMP ABY ABX ABX ABY ABY
IMM IZX IMM IZX ZP  ZP  ZP  ZP  IMP IMM IMP IMM ABS ABS ABS ABS
REL IZY IMP IZY ZPX ZPX ZPX ZPX IMP ABY IMP ABY ABX ABX ABX ABX
IMM IZX IMM IZX ZP  ZP  ZP  ZP  IMP IMM IMP IMM ABS ABS ABS ABS
REL IZY IMP IZY ZPX ZPX ZPX ZPX IMP ABY IMP ABY ABX ABX ABX ABX
"""

# Base cycles; KIL = 0 (CPU jams). Page-cross and branch penalties are added
# at run time, see PAGE_PENALTY below.
CYCLES = """
7 6 0 8 3 3 5 5 3 2 2 2 4 4 6 6
2 5 0 8 4 4 6 6 2 4 2 7 4 4 7 7
6 6 0 8 3 3 5 5 4 2 2 2 4 4 6 6
2 5 0 8 4 4 6 6 2 4 2 7 4 4 7 7
6 6 0 8 3 3 5 5 3 2 2 2 3 4 6 6
2 5 0 8 4 4 6 6 2 4 2 7 4 4 7 7
6 6 0 8 3 3 5 5 4 2 2 2 5 4 6 6
2 5 0 8 4 4 6 6 2 4 2 7 4 4 7 7
2 6 2 6 3 3 3 3 2 2 2 2 4 4 4 4
2 6 0 6 4 4 4 4 2 5 2 5 5 5 5 5
2 6 2 6 3 3 3 3 2 2 2 2 4 4 4 4
2 5 0 5 4 4 4 4 2 4 2 4 4 4 4 4
2 6 2 8 3 3 5 5 2 2 2 2 4 4 6 6
2 5 0 8 4 4 6 6 2 4 2 7 4 4 7 7
2 6 2 8 3 3 5 5 2 2 2 2 4 4 6 6
2 5 0 8 4 4 6 6 2 4 2 7 4 4 7 7
"""

SIZE = {"IMP": 1, "ACC": 1, "IMM": 2, "ZP": 2, "ZPX": 2, "ZPY": 2, "REL": 2,
        "IZX": 2, "IZY": 2, "ABS": 3, "ABX": 3, "ABY": 3, "IND": 3}

UNOFFICIAL_NAMES = {"SLO", "RLA", "SRE", "RRA", "SAX", "LAX", "DCP", "ISB", "ANC",
                    "ALR", "ARR", "XAA", "AXS", "AHX", "SHY", "SHX", "TAS", "LAS", "KIL"}

# Read instructions pay +1 cycle when ABX/ABY/IZY indexing crosses a page.
# Stores and read-modify-write always take the longer time (already in CYCLES).
PAGE_PENALTY = {"ADC", "AND", "CMP", "EOR", "LDA", "LDX", "LDY", "ORA", "SBC",
                "LAX", "LAS", "NOP"}

BRANCHES = {"BPL", "BMI", "BVC", "BVS", "BCC", "BCS", "BNE", "BEQ"}
# Instructions after which execution does not fall through to the next byte.
FLOW_END = {"JMP", "RTS", "RTI", "BRK", "KIL"}


class Op:
    __slots__ = ("code", "mn", "mode", "size", "cycles", "official")

    def __init__(self, code, mn, mode, cycles):
        self.code, self.mn, self.mode, self.cycles = code, mn, mode, cycles
        self.size = SIZE[mode]
        self.official = not (mn in UNOFFICIAL_NAMES
                             or (mn == "NOP" and code != 0xEA)
                             or (mn == "SBC" and code == 0xEB))

    @property
    def page_penalty(self):
        return self.mode in ("ABX", "ABY", "IZY") and self.mn in PAGE_PENALTY

    def __repr__(self):
        return f"Op(${self.code:02X} {self.mn} {self.mode} {self.size}B {self.cycles}c)"


def _grid(text, conv=str):
    cells = text.split()
    assert len(cells) == 256, len(cells)
    return [conv(c) for c in cells]


OPS = [Op(i, mn, mode, cyc) for i, (mn, mode, cyc) in
       enumerate(zip(_grid(MNEMONICS), _grid(MODES), _grid(CYCLES, int)))]
