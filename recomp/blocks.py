"""Split a decoded Program into basic blocks.

A block starts at every entry point, branch/jump target, instruction after a
control transfer, and wherever two decode chains meet (overlapping
instructions). It ends after a control transfer or before the next start.
Every decoded instruction belongs to exactly one block.
"""
from dataclasses import dataclass

from .ops import BRANCHES

TRANSFER = BRANCHES | {"JMP", "JSR", "RTS", "RTI", "BRK", "KIL"}


@dataclass
class Block:
    start: int
    instrs: list  # recomp.decoder.Instr, in execution order


def split_blocks(prog):
    instrs = prog.instrs
    starts = prog.block_starts()
    preds = {}
    for ins in instrs.values():
        if ins.op.mn not in TRANSFER or ins.op.mn in BRANCHES or ins.op.mn == "JSR":
            nxt = (ins.pc + ins.size) & 0xFFFF
            if ins.falls_through and nxt in instrs:
                preds[nxt] = preds.get(nxt, 0) + 1
    starts |= {pc for pc, n in preds.items() if n > 1}
    starts &= set(instrs)

    blocks, owner = [], {}
    for start in sorted(starts):
        blk = Block(start, [])
        pc = start
        while True:
            ins = instrs[pc]
            blk.instrs.append(ins)
            owner[pc] = blk
            nxt = (pc + ins.size) & 0xFFFF
            if ins.op.mn in TRANSFER or not ins.falls_through or nxt in starts or nxt not in instrs:
                break
            pc = nxt
        blocks.append(blk)
    missing = set(instrs) - set(owner)
    assert not missing, f"instructions outside any block: {sorted(missing)[:5]}"
    return blocks
