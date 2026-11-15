# -*- coding: utf-8 -*-
"""6502 fetch/decode over the 64K RAM image, producing a flat instruction
list with per-instruction cycle counts (base + static penalties; page-cross
penalties for branches/JMP/JSR are evaluated at the fixed source PC)."""

from . import ops

MODE_BYTES = {"IMP": 0, "IMM": 1, "ZP": 1, "ZPX": 1, "ZPY": 1,
             "ABS": 2, "ABSX": 2, "ABSY": 2, "ZPI": 1, "ABSI": 2, "REL": 1}


class DecodeError(Exception):
    pass


class Instr:
    __slots__ = ("pc", "op", "mn", "mode", "operand", "size", "cycles")

    def __init__(self, pc, op, mn, mode, operand, size, cycles):
        self.pc, self.op, self.mn, self.mode, self.operand = pc, op, mn, mode, operand
        self.size, self.cycles = size, cycles

    def __repr__(self):
        s = self.mn
        if self.mode == "IMM":
            s += f" #{self.operand}"
        elif self.mode == "ZP":
            s += f" ${self.operand:02X}"
        elif self.mode in ("ZPX", "ZPI"):
            s += f" ${self.operand:02X},X" if self.mode == "ZPX" else f" (${self.operand:02X},X)"
        elif self.mode == "ZPY":
            s += f" ${self.operand:02X},Y"
        elif self.mode == "ABS":
            s += f" ${self.operand:04X}"
        elif self.mode in ("ABSX", "ABSY"):
            s += f" ${self.operand:04X},{'X' if self.mode=='ABSX' else 'Y'}"
        elif self.mode == "ABSI":
            s += f" (${self.operand:04X})"
        elif self.mode == "REL":
            s += f" {self.operand:+d}"
        return s


def _next_page(pc):
    return ((pc >> 8) + 1) << 8


# Region decode stops here (no fall-through past these).
HARD_END = {"JMP", "RTS", "RTI", "BRK"}


def decode_reachable(ram, seeds, end=0xFFFA):
    """Region-based decode: from each seed (vector / JSR / JMP / branch target)
    decode straight-line until a hard terminator (JMP/RTS/RTI/BRK), an illegal
    opcode (data), an already-covered address, or `end`. Falls through branches
    and JSR (code after them is reachable). Returns list[Instr] sorted by pc,
    plus the set of region starts. end defaults to 0xFFFA (vector area)."""
    covered = set()
    all_instrs = []
    starts = set()
    worklist = list(dict.fromkeys(seeds))
    while worklist:
        pc = worklist.pop(0)
        if pc in covered or pc >= end or pc < 0:
            continue
        starts.add(pc)
        reg = []
        while pc < end and pc not in covered:
            op = ram[pc]
            entry = ops.BYOP.get(op)
            if entry is None:
                break  # data: stop the region (2A03 would NOP it; loud-stub policy)
            mn, mode, base = entry
            nb = MODE_BYTES[mode]
            ins = _make_instr(ram, pc, op, mn, mode, base, nb, end)
            covered.add(pc)
            reg.append(ins)
            pc += ins.size
            if ins.mn in HARD_END:
                break
            if ins.mn in ops.BRANCH_OPS:
                worklist.append((ins.pc + ins.size + ins.operand) & 0xFFFF)
            elif ins.mn in ("JMP", "JSR"):
                worklist.append(ins.operand)
        all_instrs += reg
    all_instrs.sort(key=lambda i: i.pc)
    return all_instrs, starts


def decode_program(ram, start, end=0xFFFA):
    """Plain linear decode of [start, end) — kept for probing/verification.
    Use decode_reachable() for real lifts (linear runs hit data)."""
    out = []
    pc = start
    while pc < end:
        op = ram[pc]
        entry = ops.BYOP.get(op)
        if entry is None:
            raise DecodeError(
                f"illegal opcode 0x{op:02X} at ${pc:04X} "
                f"(undefined on 2A03; loud-stub policy, docs/03)")
        mn, mode, base = entry
        ins = _make_instr(ram, pc, op, mn, mode, base, MODE_BYTES[mode], end)
        out.append(ins)
        pc += ins.size
    return out


def _make_instr(ram, pc, op, mn, mode, base, nb, end):
    operand = None
    if mode == "IMM":
        operand = ram[pc + 1]
    elif mode == "ZP" or mode in ("ZPX", "ZPY", "ZPI"):
        operand = ram[pc + 1]
    elif mode in ("ABS", "ABSX", "ABSY", "ABSI"):
        operand = ram[pc + 1] | (ram[pc + 2] << 8)
    elif mode == "REL":
        operand = ram[pc + 1] - 256 if ram[pc + 1] >= 0x80 else ram[pc + 1]
    size = 1 + nb
    cyc = base
    # static page-cross penalties (source PC fixed at compile time)
    if mode == "REL" and (pc >> 8) != ((pc + size + operand) >> 8):
        cyc += 1
    if mn == "JMP" and mode == "ABS" and (pc >> 8) != (operand >> 8):
        cyc += 1
    if mn == "JSR" and (pc >> 8) != (operand >> 8):
        cyc += 1
    # ZP-indexed RMW (DEC/INC/ASL/ROL/LSR/ROR in ZPX mode) has a +1 page-
    # cross penalty when (operand + X) >= 256. That is data-dependent (X is a
    # runtime value), so codegen emits it as a runtime add to rt_extra;
    # nothing is added here (operand is 8-bit, a static test would never fire).
    if pc + size > end and mode != "REL":
        raise DecodeError(f"instruction at ${pc:04X} crosses decode end")
    return Instr(pc, op, mn, mode, operand, size, cyc)


def branch_targets(instrs):
    """All forward/backward branch and absolute-jump/call targets. A block
    must start at each target: backward branches can land mid-block, and the
    x86 output jumps to block labels only."""
    t = set()
    for ins in instrs:
        if ins.mn in ops.BRANCH_OPS:
            t.add((ins.pc + ins.size + ins.operand) & 0xFFFF)
        elif ins.mn in ("JMP", "JSR"):
            t.add(ins.operand)
    return t


def build_blocks(instrs, targets=None, starts=None):
    """Split the (possibly multi-region) instruction list into basic blocks.
    A block ends after the first block-ending control op (branch/JMP/JSR/RTS/
    RTI/BRK). A block also starts at every branch/jump target, every region
    start (`starts`, e.g. vectors), and before every PPU I/O instruction (the
    driver performs the access at the block boundary).
    Returns list of dicts: {"start", "end", "instrs", "exit_pc", "cycles"}"""
    if targets is None:
        targets = branch_targets(instrs)
    if starts is None:
        starts = set()
    blocks = []
    cur = []
    i = 0
    n = len(instrs)
    while i < n:
        ins = instrs[i]
        # PPU I/O: split before (access happens at a PPU-position boundary)
        if ops.is_ppu_io(ins.mn, ins.mode, ins.operand) and cur:
            _close(blocks, cur, None)
            cur = []
        # every branch/jump target and region start is a block start
        if (ins.pc in targets or ins.pc in starts) and cur:
            _close(blocks, cur, None)
            cur = []
        cur.append(ins)
        if ins.mn in ops.BLOCK_END:
            if ins.mn in ops.BRANCH_OPS:
                tgt = (ins.pc + ins.size + ins.operand) & 0xFFFF
            elif ins.mn in ("JMP", "JSR"):
                tgt = ins.operand
            else:
                tgt = None
            _close(blocks, cur, tgt)
            cur = []
            i += 1
            continue
        i += 1
    if cur:
        _close(blocks, cur, None)
    return blocks


def _close(blocks, instrs, exit_pc):
    last = instrs[-1]
    blocks.append({
        "start": instrs[0].pc,
        "end": instrs[-1].pc + instrs[-1].size,
        "instrs": instrs,
        "exit_pc": exit_pc if exit_pc is not None else instrs[-1].pc + instrs[-1].size,
        "cycles": sum(x.cycles for x in instrs),
    })
