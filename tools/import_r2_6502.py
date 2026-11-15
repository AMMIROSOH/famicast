"""Build tests/data/r2_6502.json from radare2's 6502 disassembler tables.

radare2 (LGPL-3.0) decodes 6502 in two steps (libr/arch/p/6502/6502dis.inc.c):
its own list for the unofficial opcodes, then a fallback to the 65816 table
(libr/arch/p/snes/optable.h) with M=X=1 (8-bit mode) for everything else.
We replay that logic here and store, per opcode: mnemonic, mode, size.

The JSON is committed so tests do not need a radare2 checkout.
Usage: python tools/import_r2_6502.py C:/projects/radare2
"""
import json
import os
import re
import sys

BRANCHES = {"bpl", "bmi", "bvc", "bvs", "bcc", "bcs", "bne", "beq"}


def parse_6502_list(path):
    ops = {}
    for m in re.finditer(r'^\s*\{0x([0-9a-f]{2}), "([^"]+)", (\d)\}', open(path).read(), re.M):
        op = int(m.group(1), 16)
        ops.setdefault(op, (m.group(2), int(m.group(3))))  # first match wins, like the C loop
    return ops


def parse_snes_table(path):
    rows = re.findall(r'^\{ "([^"]+)",\s*(SNES_OP_\w+)\}', open(path).read(), re.M)
    assert len(rows) == 256, len(rows)
    size = {"SNES_OP_8BIT": 1, "SNES_OP_16BIT": 2, "SNES_OP_24BIT": 3, "SNES_OP_32BIT": 4,
            "SNES_OP_IMM_M": 2, "SNES_OP_IMM_X": 2}  # M=X=1
    out = []
    for name, flag in rows:
        if flag in ("SNES_OP_IMM_M", "SNES_OP_IMM_X"):
            name += " #0x%02x"
        out.append((name, size[flag]))
    return out


def mode_of(text):
    """Turn a radare2 format string into (mnemonic, mode)."""
    mn, _, arg = text.partition(" ")
    arg = arg.replace(" ", "")
    if mn in BRANCHES:
        return mn, "REL"
    table = {
        "": "IMP", "a": "ACC", "#0x%02x": "IMM",
        "0x%02x": "ZP", "0x%02x,x": "ZPX", "0x%02x,y": "ZPY",
        "0x%04x": "ABS", "0x%04x,x": "ABX", "0x%04x,y": "ABY",
        "(0x%04x)": "IND", "(0x%02x,x)": "IZX", "(0x%02x),y": "IZY",
    }
    return mn, table.get(arg, "OTHER:" + arg)  # OTHER = 65816-only form


def main(r2_root):
    base = os.path.join(r2_root, "libr", "arch", "p")
    six = parse_6502_list(os.path.join(base, "6502", "6502dis.inc.c"))
    snes = parse_snes_table(os.path.join(base, "snes", "optable.h"))
    rows = {}
    for op in range(256):
        text, size = six.get(op) or snes[op]
        mn, mode = mode_of(text)
        rows["%02X" % op] = {"mn": mn, "mode": mode, "size": size,
                             "src": "6502" if op in six else "snes"}
    out = {
        "_source": "radare2 libr/arch/p/6502/6502dis.inc.c + libr/arch/p/snes/optable.h (M=X=1)",
        "_license": "LGPL-3.0 (radare2); data extracted by tools/import_r2_6502.py",
        "ops": rows,
    }
    dst = os.path.join(os.path.dirname(__file__), "..", "tests", "data", "r2_6502.json")
    with open(dst, "w", newline="\n") as f:
        json.dump(out, f, indent=1, sort_keys=True)
    print("wrote", os.path.normpath(dst))


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "C:/projects/radare2")
