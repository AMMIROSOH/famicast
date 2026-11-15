"""Recompile an NES ROM to C.

    python -m recomp ROM -o OUT.c [--hints GAME.toml]

GAME.toml (all keys optional):
    entries   = [0xCE67, ...]   # extra code entry points (jump tables, RTS/RTI tricks)
    no_return = [0xD123, ...]   # subroutines that never return to the caller
Addresses the runtime had to interpret are printed by
`nes_headless ROM --misses`; add the ROM ones as entries.
"""
import argparse
import os
import sys
import tomllib

from .blocks import split_blocks
from .decoder import explore
from .emit_c import emit, fnv1a
from .ines import Cartridge


def main(argv=None):
    ap = argparse.ArgumentParser(prog="python -m recomp", description=__doc__.split("\n")[1])
    ap.add_argument("rom")
    ap.add_argument("-o", "--output", required=True)
    ap.add_argument("--hints", help="per-game TOML with extra entries")
    args = ap.parse_args(argv)

    cart = Cartridge.load(args.rom)
    mem = cart.nrom_image()
    hints = {}
    if args.hints:
        with open(args.hints, "rb") as f:
            hints = tomllib.load(f)
    entries = [v for v in cart.vectors() if v >= 0x8000] + list(hints.get("entries", []))
    prog = explore(mem, entries, no_return=set(hints.get("no_return", [])))
    blocks = split_blocks(prog)
    src = emit(blocks, mem, os.path.basename(args.rom), fnv1a(cart.prg))
    os.makedirs(os.path.dirname(os.path.abspath(args.output)), exist_ok=True)
    with open(args.output, "w", newline="\n") as f:
        f.write(src)
    print(f"{os.path.basename(args.rom)}: {cart.summary()}")
    print(f"  {len(prog.instrs)} instructions, {len(blocks)} blocks, "
          f"{len(prog.indirect)} indirect jumps, {len(prog.unofficial)} unofficial opcodes -> {args.output}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
