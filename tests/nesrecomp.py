"""Test helpers that drive the C recompiler (build/nesrecomp, nes/recomp/main.c):
its opcode table, iNES summary, and the instructions and blocks it emits,
read back from the disassembly comments of the generated C."""
import os
import re
import subprocess
import tempfile
from collections import namedtuple

from tests.games import ROOT

BUILD = os.environ.get("NES_BUILD", os.path.join(ROOT, "build"))
EXE = ".exe" if os.name == "nt" else ""
NESRECOMP = os.path.join(BUILD, "nesrecomp" + EXE)
BUILT = os.path.exists(NESRECOMP)

Op = namedtuple("Op", "code mn mode size cycles official page_penalty")
Ins = namedtuple("Ins", "pc raw unofficial mn arg")

BRANCHES = {"BPL", "BMI", "BVC", "BVS", "BCC", "BCS", "BNE", "BEQ"}
FLOW_END = {"JMP", "RTS", "RTI", "BRK", "KIL"}


def run(*args, check=True):
    return subprocess.run([NESRECOMP, *args], capture_output=True, text=True, check=check)


def ops():
    out = []
    for line in run("--ops").stdout.splitlines():
        code, mn, mode, size, cycles, official, penalty = line.split()
        out.append(Op(int(code, 16), mn, mode, int(size), int(cycles), official == "1", penalty == "1"))
    return out


def make_rom(prg=b"", prg_units=1, chr_units=1, f6=0, f7=0, trainer=False, vectors=(0xD400, 0xC070, 0xC070)):
    """iNES file: `prg` at the start of PRG, the vectors (NMI, RESET, IRQ) at its end."""
    hdr = b"NES\x1a" + bytes([prg_units, chr_units, f6 | (0x04 if trainer else 0), f7]) + bytes(8)
    data = bytearray(prg_units * 0x4000)
    data[:len(prg)] = prg
    for i, v in enumerate(vectors):
        at = len(data) - 6 + i * 2
        data[at:at + 2] = v.to_bytes(2, "little")
    return hdr + (bytes(512) if trainer else b"") + bytes(data) + bytes(chr_units * 0x2000)


def target(ins):
    """Static branch/JMP abs/JSR target from the disassembly, else None."""
    if ins.mn in BRANCHES or (ins.mn in ("JMP", "JSR") and ins.arg.startswith("$")):
        return int(ins.arg[1:], 16)
    return None


def falls_through(ins):
    return ins.mn not in FLOW_END


_FUNC = re.compile(r"^static void (b_\w+)\(Cpu \*c\) \{$")
_INS = re.compile(r"^i_([0-9A-F]{4}): /\* [0-9A-F]{4}  ((?:[0-9A-F]{2} ?)+?)\s+(\*?)([A-Z]{3}) ?(.*) \*/$")


class Listing:
    """What nesrecomp emitted: instrs {pc: Ins} and blocks {name: [pc, ...]}."""

    def __init__(self, c_source):
        self.instrs, self.blocks, cur = {}, {}, None
        for line in c_source.splitlines():
            m = _FUNC.match(line)
            if m:
                cur = self.blocks.setdefault(m.group(1), [])
                continue
            m = _INS.match(line)
            if m:
                pc = int(m.group(1), 16)
                self.instrs[pc] = Ins(pc, bytes.fromhex(m.group(2)), m.group(3) == "*", m.group(4), m.group(5))
                cur.append(pc)


def recompile(rom, entries=(), banked=(), no_return=(), misses=None):
    """Recompile `rom` (a path or iNES bytes) with these hints; returns
    (Listing, saved hints text or None)."""
    with tempfile.TemporaryDirectory() as tmp:
        if isinstance(rom, (bytes, bytearray)):
            path = os.path.join(tmp, "test.nes")
            with open(path, "wb") as f:
                f.write(rom)
            rom = path
        hints = os.path.join(tmp, "hints.toml")
        with open(hints, "w") as f:
            f.write("entries = [%s]\n" % ", ".join(f"0x{a:04X}" for a in entries))
            f.write("banked = [%s]\n" % ", ".join(f"[{b}, 0x{a:04X}]" for b, a in banked))
            f.write("no_return = [%s]\n" % ", ".join(f"0x{a:04X}" for a in no_return))
        args = [rom, "-o", os.path.join(tmp, "out.c"), "--hints", hints]
        if misses is not None:
            with open(os.path.join(tmp, "misses.txt"), "w") as f:
                f.write(misses)
            args += ["--misses", os.path.join(tmp, "misses.txt"), "--save-hints", os.path.join(tmp, "saved.toml")]
        run(*args)
        with open(os.path.join(tmp, "out.c")) as f:
            listing = Listing(f.read())
        saved = None
        if misses is not None:
            with open(os.path.join(tmp, "saved.toml")) as f:
                saved = f.read()
        return listing, saved
