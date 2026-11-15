"""Recursive-descent 6502 decoder.

Instructions are keyed by address, so two decodes may overlap (e.g. the SMB
`BIT abs` trick, where the operand bytes are also an instruction reached by a
jump). Code we cannot follow statically (JMP (ind), RTS used as a jump,
jumps into RAM) is recorded; the runtime handles it with the interpreter
fallback (docs/02).
"""
from dataclasses import dataclass, field

from .ops import OPS, BRANCHES, FLOW_END


@dataclass(frozen=True)
class Instr:
    pc: int
    op: object          # recomp.ops.Op
    operand: int        # byte or word value; 0 for IMP/ACC
    size: int

    @property
    def target(self):
        """Static control-flow target (branch, JMP abs, JSR), else None."""
        if self.op.mn in BRANCHES:
            return (self.pc + 2 + (self.operand - 256 if self.operand >= 0x80 else self.operand)) & 0xFFFF
        if self.op.mn in ("JMP", "JSR") and self.op.mode == "ABS":
            return self.operand
        return None

    @property
    def falls_through(self):
        return self.op.mn not in FLOW_END


def decode_at(mem, pc):
    op = OPS[mem[pc]]
    if op.size == 2:
        operand = mem[(pc + 1) & 0xFFFF]
    elif op.size == 3:
        operand = mem[(pc + 1) & 0xFFFF] | mem[(pc + 2) & 0xFFFF] << 8
    else:
        operand = 0
    return Instr(pc, op, operand, op.size)


@dataclass
class Program:
    instrs: dict = field(default_factory=dict)     # pc -> Instr
    entries: set = field(default_factory=set)      # vectors + hints + JSR targets
    indirect: set = field(default_factory=set)     # pcs of JMP (ind)
    external: set = field(default_factory=set)     # targets outside the code range
    unofficial: set = field(default_factory=set)   # pcs of unofficial opcodes

    def block_starts(self):
        """Addresses where a basic block must begin."""
        starts = set(self.entries)
        for ins in self.instrs.values():
            t = ins.target
            if t is not None and t in self.instrs:
                starts.add(t)
            if ins.op.mn in BRANCHES or ins.op.mn in ("JSR", "JMP", "RTS", "RTI", "BRK", "KIL"):
                nxt = (ins.pc + ins.size) & 0xFFFF
                if nxt in self.instrs:
                    starts.add(nxt)
        return starts


def explore(mem, entries, code_range=(0x8000, 0x10000), no_return=()):
    """Follow all statically known control flow from `entries`.
    `no_return`: subroutines that never return to the caller (e.g. jump-table
    dispatchers that pop the return address and read data after the JSR)."""
    lo, hi = code_range
    prog = Program(entries=set(entries))
    work = list(entries)
    while work:
        pc = work.pop()
        if pc in prog.instrs:
            continue
        if not lo <= pc < hi:
            prog.external.add(pc)
            continue
        ins = decode_at(mem, pc)
        if pc + ins.size > hi:  # operand bytes in the next window (maybe another bank)
            prog.external.add(pc)
            continue
        prog.instrs[pc] = ins
        if not ins.op.official:
            prog.unofficial.add(pc)
        mn = ins.op.mn
        if mn == "JMP" and ins.op.mode == "IND":
            prog.indirect.add(pc)
        t = ins.target
        if t is not None:
            work.append(t)
            if mn == "JSR":
                prog.entries.add(t)
        if ins.falls_through and not (mn == "JSR" and t in no_return):
            work.append((pc + ins.size) & 0xFFFF)
    return prog
