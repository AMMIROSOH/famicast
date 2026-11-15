"""C code for one 6502 instruction, generated from recomp/ops.py.

Used for both the interpreter's switch (tools/gen_cpu_switch.py) and, later,
the recompiler's block functions, so the instruction semantics exist once and
are verified once (nestest + SingleStepTests, docs/06).

Every rd()/wr() is one CPU cycle, including the dummy reads and writes the
real 2A03 performs (sequences from 6502_cpu.txt / nesdev; checked cycle by
cycle against SingleStepTests). The opcode fetch is done by the caller.
Helpers (rd, wr, push, pull, op_*) live in runtime/cpu_ops.h.
"""
from .ops import BRANCHES

READ = {"LDA", "LDX", "LDY", "ADC", "SBC", "AND", "ORA", "EOR", "CMP", "CPX", "CPY", "BIT",
        "LAX", "NOP", "LAS", "ANC", "ALR", "ARR", "AXS", "XAA"}
STORE = {"STA": "c->a", "STX": "c->x", "STY": "c->y", "SAX": "c->a & c->x"}
UNSTABLE_STORE = {"AHX", "SHX", "SHY", "TAS"}  # value & (high byte + 1), see _unstable_store
RMW = {"ASL", "LSR", "ROL", "ROR", "INC", "DEC", "SLO", "RLA", "SRE", "RRA", "DCP", "ISB"}

IMPLIED = {
    "CLC": "c->p &= ~FLAG_C;", "SEC": "c->p |= FLAG_C;",
    "CLI": "c->p &= ~FLAG_I;", "SEI": "c->p |= FLAG_I;",
    "CLD": "c->p &= ~FLAG_D;", "SED": "c->p |= FLAG_D;", "CLV": "c->p &= ~FLAG_V;",
    "TAX": "c->x = c->a; nz(c, c->x);", "TAY": "c->y = c->a; nz(c, c->y);",
    "TXA": "c->a = c->x; nz(c, c->a);", "TYA": "c->a = c->y; nz(c, c->a);",
    "TSX": "c->x = c->s; nz(c, c->x);", "TXS": "c->s = c->x;",
    "INX": "c->x++; nz(c, c->x);", "INY": "c->y++; nz(c, c->y);",
    "DEX": "c->x--; nz(c, c->x);", "DEY": "c->y--; nz(c, c->y);",
    "NOP": "",
}

BRANCH_COND = {"BPL": "!(c->p & FLAG_N)", "BMI": "(c->p & FLAG_N)",
               "BVC": "!(c->p & FLAG_V)", "BVS": "(c->p & FLAG_V)",
               "BCC": "!(c->p & FLAG_C)", "BCS": "(c->p & FLAG_C)",
               "BNE": "!(c->p & FLAG_Z)", "BEQ": "(c->p & FLAG_Z)"}

SPECIAL = {
    "JSR": ["uint16_t lo = rd(c, c->pc++);", "rd(c, 0x100 | c->s);",
            "push(c, c->pc >> 8);", "push(c, c->pc & 0xFF);",
            "c->pc = lo | rd(c, c->pc) << 8;"],
    "RTS": ["rd(c, c->pc);", "rd(c, 0x100 | c->s);", "uint16_t lo = pull(c);",
            "c->pc = lo | pull(c) << 8;", "rd(c, c->pc);", "c->pc++;"],
    "RTI": ["rd(c, c->pc);", "rd(c, 0x100 | c->s);", "c->p = (pull(c) & ~FLAG_B) | FLAG_U;",
            "uint16_t lo = pull(c);", "c->pc = lo | pull(c) << 8;"],
    "BRK": ["rd(c, c->pc++);", "push(c, c->pc >> 8);", "push(c, c->pc & 0xFF);",
            "push(c, c->p | FLAG_B | FLAG_U);", "c->p |= FLAG_I;",
            "uint16_t lo = rd(c, 0xFFFE);", "c->pc = lo | rd(c, 0xFFFF) << 8;"],
    "PHA": ["rd(c, c->pc);", "push(c, c->a);"],
    "PHP": ["rd(c, c->pc);", "push(c, c->p | FLAG_B | FLAG_U);"],
    "PLA": ["rd(c, c->pc);", "rd(c, 0x100 | c->s);", "c->a = pull(c);", "nz(c, c->a);"],
    "PLP": ["rd(c, c->pc);", "rd(c, 0x100 | c->s);", "c->p = (pull(c) & ~FLAG_B) | FLAG_U;"],
    "KIL": ["c->jammed = 1;"],
}


def _ea(mode, kind):
    """Lines that leave the effective address in `ea`. kind: read/write/rmw.
    Indexed modes read the un-carried address first when the index crosses a
    page (reads), or always (writes and read-modify-write)."""
    fix = ("if ((ea ^ base) & 0xFF00) " if kind == "read" else "") + \
          "rd(c, (base & 0xFF00) | (ea & 0xFF));"
    return {
        "ZP": ["uint16_t ea = rd(c, c->pc++);"],
        "ZPX": ["uint8_t zp = rd(c, c->pc++);", "rd(c, zp);", "uint16_t ea = (uint8_t)(zp + c->x);"],
        "ZPY": ["uint8_t zp = rd(c, c->pc++);", "rd(c, zp);", "uint16_t ea = (uint8_t)(zp + c->y);"],
        "ABS": ["uint16_t ea = rd(c, c->pc++);", "ea |= rd(c, c->pc++) << 8;"],
        "ABX": ["uint16_t base = rd(c, c->pc++);", "base |= rd(c, c->pc++) << 8;",
                "uint16_t ea = base + c->x;", fix],
        "ABY": ["uint16_t base = rd(c, c->pc++);", "base |= rd(c, c->pc++) << 8;",
                "uint16_t ea = base + c->y;", fix],
        "IZX": ["uint8_t zp = rd(c, c->pc++);", "rd(c, zp);", "zp += c->x;",
                "uint16_t ea = rd(c, zp);", "ea |= rd(c, (uint8_t)(zp + 1)) << 8;"],
        "IZY": ["uint8_t zp = rd(c, c->pc++);", "uint16_t base = rd(c, zp);",
                "base |= rd(c, (uint8_t)(zp + 1)) << 8;", "uint16_t ea = base + c->y;", fix],
    }[mode]


def _unstable_store(op):
    """SHX/SHY/AHX/TAS: the stored value is ANDed with (base high byte + 1),
    and on a page cross that value also replaces the address high byte
    (nesdev 'CPU unofficial opcodes'; verified by SingleStepTests)."""
    val = {"SHY": "c->y", "SHX": "c->x", "AHX": "c->a & c->x", "TAS": "c->s"}[op.mn]
    lines = _ea(op.mode, "write")
    if op.mn == "TAS":
        lines.append("c->s = c->a & c->x;")
    return lines + ["uint8_t v = %s & (uint8_t)((base >> 8) + 1);" % val,
                    "if ((ea ^ base) & 0xFF00) ea = (ea & 0xFF) | (v << 8);",
                    "wr(c, ea, v);"]


def body(op):
    """List of C statements executing `op` after its opcode byte was fetched."""
    mn, mode = op.mn, op.mode
    if mn in SPECIAL:
        return list(SPECIAL[mn])
    if mn in BRANCHES:
        return ["int8_t off = (int8_t)rd(c, c->pc++);",
                f"if ({BRANCH_COND[mn]}) {{",
                "    rd(c, c->pc);",
                "    uint16_t t = c->pc + off;",
                "    if ((t ^ c->pc) & 0xFF00) rd(c, (c->pc & 0xFF00) | (t & 0xFF));",
                "    c->pc = t;",
                "}"]
    if mn == "JMP":
        if mode == "ABS":
            return ["uint16_t lo = rd(c, c->pc++);", "c->pc = lo | rd(c, c->pc) << 8;"]
        return ["uint16_t ptr = rd(c, c->pc++);", "ptr |= rd(c, c->pc++) << 8;",
                "uint16_t lo = rd(c, ptr);",
                "c->pc = lo | rd(c, (ptr & 0xFF00) | ((ptr + 1) & 0xFF)) << 8;"]  # page-wrap bug
    if mode == "IMP":
        return ["rd(c, c->pc);", IMPLIED[mn]]
    if mode == "ACC":
        return ["rd(c, c->pc);", f"c->a = op_{mn}(c, c->a);"]
    if mn in READ:
        fn = "op_LXA" if (mn, mode) == ("LAX", "IMM") else f"op_{mn}"
        if mode == "IMM":
            return [f"{fn}(c, rd(c, c->pc++));"]
        return _ea(mode, "read") + [f"{fn}(c, rd(c, ea));"]
    if mn in STORE:
        return _ea(mode, "write") + [f"wr(c, ea, {STORE[mn]});"]
    if mn in UNSTABLE_STORE:
        return _unstable_store(op)
    if mn in RMW:
        return _ea(mode, "rmw") + ["uint8_t v = rd(c, ea);", "wr(c, ea, v);",
                                   f"v = op_{mn}(c, v);", "wr(c, ea, v);"]
    raise ValueError(f"no C template for {op}")
