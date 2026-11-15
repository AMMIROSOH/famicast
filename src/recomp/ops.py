# -*- coding: utf-8 -*-
"""6502 opcode table — single source of truth for the recompiler.

Format: (opcode, mnemonic, mode, base_cycles)
Modes: IMP, IMM, ZP, ZPX, ZPY, ABS, ABSX, ABSY, ZPI, ABSI, REL

153 entries = the 151-opcode MOS 6502 core (verified 2026-11-15 against
lasect/pg_6502's opcode_table, init/04_opcodes.sql — 0 mnemonic/mode diffs;
see docs/06) + the two guide-documented NOPs 0x02/0x0B.

TARGET IS THE 2A03 (Ricoh, MOS-6502 core with BCD disabled), so:
  * 0x0A=ASL A, 0x2A=ROL A, 0x4A=LSR A, 0x6A=ROR A  (accumulator forms, 2c)
  * PHX/PLX/PHY/PLY do NOT exist (those are 65SC/65C02; 0x0A/0x2A/0x5A/0x7A
    are undefined on the 2A03)
  * STA abs,X = 4 cycles (2A03; classic 6502 = 5) — see 0x9D below
  * 103 undefined bytes decode as illegal: decoder raises DecodeError
    (loud-stub policy, docs/03). The 2A03 executes them as 2-cycle NOPs
    (some with documented 6502 side effects) — a v2 emulation concern.

Cycle counts: base cycles, nesdev "CPU instructions" / 2A03 table.
Variable-cycle ops (branch taken, page-cross RMW) get base cycles; the
runtime formula in decoder.py adds the +1 penalties statically.
"""

OPS = [
    (0x00, 'BRK', 'IMP', 7), (0x01, 'ORA', 'ZPI', 5), (0x02, 'NOP', 'IMP', 2), (0x05, 'ORA', 'ZP', 3),
    (0x06, 'ASL', 'ZP', 5), (0x08, 'PHP', 'IMP', 3), (0x09, 'ORA', 'IMM', 2), (0x0A, 'ASL', 'IMP', 2),
    (0x0B, 'NOP', 'IMP', 2), (0x0D, 'ORA', 'ABS', 4), (0x0E, 'ASL', 'ABS', 6), (0x10, 'BPL', 'REL', 2),
    (0x11, 'ORA', 'ABSI', 5), (0x15, 'ORA', 'ZPX', 4), (0x16, 'ASL', 'ZPX', 6), (0x18, 'CLC', 'IMP', 2),
    (0x19, 'ORA', 'ABSY', 4), (0x1D, 'ORA', 'ABSX', 4), (0x1E, 'ASL', 'ABSX', 7), (0x20, 'JSR', 'ABS', 6),
    (0x21, 'AND', 'ZPI', 5), (0x24, 'BIT', 'ZP', 3), (0x25, 'AND', 'ZP', 3), (0x26, 'ROL', 'ZP', 5),
    (0x28, 'PLP', 'IMP', 4), (0x29, 'AND', 'IMM', 2), (0x2A, 'ROL', 'IMP', 2), (0x2C, 'BIT', 'ABS', 4),
    (0x2D, 'AND', 'ABS', 4), (0x2E, 'ROL', 'ABS', 6), (0x30, 'BMI', 'REL', 2), (0x31, 'AND', 'ABSI', 5),
    (0x35, 'AND', 'ZPX', 4), (0x36, 'ROL', 'ZPX', 6), (0x38, 'SEC', 'IMP', 2), (0x39, 'AND', 'ABSY', 4),
    (0x3D, 'AND', 'ABSX', 4), (0x3E, 'ROL', 'ABSX', 7), (0x40, 'RTI', 'IMP', 6), (0x41, 'EOR', 'ZPI', 5),
    (0x45, 'EOR', 'ZP', 3), (0x46, 'LSR', 'ZP', 5), (0x48, 'PHA', 'IMP', 3), (0x49, 'EOR', 'IMM', 2),
    (0x4A, 'LSR', 'IMP', 2), (0x4C, 'JMP', 'ABS', 3), (0x4D, 'EOR', 'ABS', 4), (0x4E, 'LSR', 'ABS', 6),
    (0x50, 'BVC', 'REL', 2), (0x51, 'EOR', 'ABSI', 5), (0x55, 'EOR', 'ZPX', 4), (0x56, 'LSR', 'ZPX', 6),
    (0x58, 'CLI', 'IMP', 2), (0x59, 'EOR', 'ABSY', 4), (0x5D, 'EOR', 'ABSX', 4), (0x5E, 'LSR', 'ABSX', 7),
    (0x60, 'RTS', 'IMP', 6), (0x61, 'ADC', 'ZPI', 5), (0x65, 'ADC', 'ZP', 3), (0x66, 'ROR', 'ZP', 5),
    (0x68, 'PLA', 'IMP', 4), (0x69, 'ADC', 'IMM', 2), (0x6A, 'ROR', 'IMP', 2), (0x6C, 'JMP', 'ABSI', 5),
    (0x6D, 'ADC', 'ABS', 4), (0x6E, 'ROR', 'ABS', 6), (0x70, 'BVS', 'REL', 2), (0x71, 'ADC', 'ABSI', 5),
    (0x75, 'ADC', 'ZPX', 4), (0x76, 'ROR', 'ZPX', 6), (0x78, 'SEI', 'IMP', 2), (0x79, 'ADC', 'ABSY', 4),
    (0x7D, 'ADC', 'ABSX', 4), (0x7E, 'ROR', 'ABSX', 7), (0x81, 'STA', 'ZPI', 5), (0x84, 'STY', 'ZP', 3),
    (0x85, 'STA', 'ZP', 3), (0x86, 'STX', 'ZP', 3), (0x88, 'DEY', 'IMP', 2), (0x8A, 'TXA', 'IMP', 2),
    (0x8C, 'STY', 'ABS', 4), (0x8D, 'STA', 'ABS', 4), (0x8E, 'STX', 'ABS', 4), (0x90, 'BCC', 'REL', 2),
    (0x91, 'STA', 'ABSI', 5), (0x94, 'STY', 'ZPX', 4), (0x95, 'STA', 'ZPX', 4), (0x96, 'STX', 'ZPY', 4),
    (0x98, 'TYA', 'IMP', 2), (0x99, 'STA', 'ABSY', 5), (0x9A, 'TXS', 'IMP', 2), (0x9D, 'STA', 'ABSX', 4),
    (0xA0, 'LDY', 'IMM', 2), (0xA1, 'LDA', 'ZPI', 5), (0xA2, 'LDX', 'IMM', 2), (0xA4, 'LDY', 'ZP', 3),
    (0xA5, 'LDA', 'ZP', 3), (0xA6, 'LDX', 'ZP', 3), (0xA8, 'TAY', 'IMP', 2), (0xA9, 'LDA', 'IMM', 2),
    (0xAA, 'TAX', 'IMP', 2), (0xAC, 'LDY', 'ABS', 4), (0xAD, 'LDA', 'ABS', 4), (0xAE, 'LDX', 'ABS', 4),
    (0xB0, 'BCS', 'REL', 2), (0xB1, 'LDA', 'ABSI', 5), (0xB4, 'LDY', 'ZPX', 4), (0xB5, 'LDA', 'ZPX', 4),
    (0xB6, 'LDX', 'ZPY', 4), (0xB8, 'CLV', 'IMP', 2), (0xB9, 'LDA', 'ABSY', 4), (0xBA, 'TSX', 'IMP', 2),
    (0xBC, 'LDY', 'ABSX', 4), (0xBD, 'LDA', 'ABSX', 4), (0xBE, 'LDX', 'ABSY', 5), (0xC0, 'CPY', 'IMM', 2),
    (0xC1, 'CMP', 'ZPI', 5), (0xC4, 'CPY', 'ZP', 3), (0xC5, 'CMP', 'ZP', 3), (0xC6, 'DEC', 'ZP', 5),
    (0xC8, 'INY', 'IMP', 2), (0xC9, 'CMP', 'IMM', 2), (0xCA, 'DEX', 'IMP', 2), (0xCC, 'CPY', 'ABS', 4),
    (0xCD, 'CMP', 'ABS', 4), (0xCE, 'DEC', 'ABS', 6), (0xD0, 'BNE', 'REL', 2), (0xD1, 'CMP', 'ABSI', 5),
    (0xD5, 'CMP', 'ZPX', 4), (0xD6, 'DEC', 'ZPX', 6), (0xD8, 'CLD', 'IMP', 2), (0xD9, 'CMP', 'ABSY', 4),
    (0xDD, 'CMP', 'ABSX', 4), (0xDE, 'DEC', 'ABSX', 7), (0xE0, 'CPX', 'IMM', 2), (0xE1, 'SBC', 'ZPI', 5),
    (0xE4, 'CPX', 'ZP', 3), (0xE5, 'SBC', 'ZP', 3), (0xE6, 'INC', 'ZP', 5), (0xE8, 'INX', 'IMP', 2),
    (0xE9, 'SBC', 'IMM', 2), (0xEA, 'NOP', 'IMP', 2), (0xEC, 'CPX', 'ABS', 4), (0xED, 'SBC', 'ABS', 4),
    (0xEE, 'INC', 'ABS', 6), (0xF0, 'BEQ', 'REL', 2), (0xF1, 'SBC', 'ABSI', 5), (0xF5, 'SBC', 'ZPX', 4),
    (0xF6, 'INC', 'ZPX', 6), (0xF8, 'SED', 'IMP', 2), (0xF9, 'SBC', 'ABSY', 4), (0xFD, 'SBC', 'ABSX', 4),
    (0xFE, 'INC', 'ABSX', 7),
]

BYOP = {op: (mn, mode, cyc) for op, mn, mode, cyc in OPS}
assert len(BYOP) == 153, f"expected 153 unique opcodes, got {len(BYOP)}"

# Anchors that historically get corrupted (see docs/06-verification-log.md).
_ANCHORS = {
    0x00: "BRK", 0x0A: "ASL", 0x2A: "ROL", 0x4A: "LSR", 0x6A: "ROR",
    0x20: "JSR", 0x60: "RTS", 0x40: "RTI", 0x4C: "JMP", 0x6C: "JMP",
    0x08: "PHP", 0x28: "PLP", 0x48: "PHA", 0x68: "PLA",
    0x8A: "TXA", 0x8C: "STY", 0x9A: "TXS", 0xBA: "TSX", 0xA2: "LDX",
    0xA9: "LDA", 0xEA: "NOP", 0x02: "NOP", 0x0B: "NOP",
}
for _op, _mn in _ANCHORS.items():
    assert BYOP[_op][0] == _mn, f"anchor mismatch 0x{_op:02X}: {_mn} != {BYOP[_op][0]}"

BRANCH_OPS = {"BNE", "BEQ", "BCC", "BCS", "BMI", "BPL", "BVC", "BVS"}
# x86 `test [P], bit` sets ZF=1 iff the 6502 bit is CLEAR. So a branch
# "taken when bit==0" maps to jz, "taken when bit==1" to jnz:
#   BNE (Z=0) -> bit0 clear -> jz        BEQ (Z=1) -> bit0 set -> jnz
#   BCC (C=0) -> bit1 clear -> jz        BCS (C=1) -> bit1 set -> jnz
#   BMI (N=1) -> bit7 set   -> jnz       BPL (N=0) -> bit7 clear -> jz
#   BVC (V=0) -> bit6 clear -> jz        BVS (V=1) -> bit6 set -> jnz
BRANCH_TEST = {"BNE": (0x01, "jz"),  "BEQ": (0x01, "jnz"),
               "BCC": (0x02, "jz"),  "BCS": (0x02, "jnz"),
               "BMI": (0x80, "jnz"), "BPL": (0x80, "jz"),
               "BVC": (0x40, "jz"),  "BVS": (0x40, "jnz")}

# Ops that end a basic block.
BLOCK_END = BRANCH_OPS | {"JMP", "JSR", "RTS", "RTI", "BRK"}

# I/O: blocks are split before these so the driver performs the access at
# the exact PPU position. PPU $2000-$2004 (2A03/2C02); APU $2005-$2007 and
# $4000-$4017 (stubbed no-ops at this tier).
IO_PPU = 1
IO_APU = 2

def io_kind(mn, mode, operand=None):
    """0 = normal memory, IO_PPU, or IO_APU."""
    if mn not in ("LDA", "STA") or mode != "ABS":
        return 0
    if operand is None:
        return IO_PPU  # unresolved -> treat as PPU I/O (safe: extra split)
    if 0x2000 <= operand <= 0x2004:
        return IO_PPU
    if 0x2005 <= operand <= 0x2007 or 0x4000 <= operand <= 0x4017:
        return IO_APU
    return 0

def is_ppu_io(mn, mode, operand=None):  # kept for compatibility
    return io_kind(mn, mode, operand) != 0
