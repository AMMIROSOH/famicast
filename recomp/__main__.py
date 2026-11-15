"""Recompile an NES ROM to C.

    python -m recomp ROM -o OUT.c [--hints GAME.toml]

GAME.toml (all keys optional):
    entries   = [0xCE67, ...]          # extra entry points in assumed-bank windows
    banked    = [[5, 0x8123], ...]     # [8 KB PRG bank, address] entry points
    no_return = [0xD123, ...]          # subroutines that never return to the caller
Addresses the runtime had to interpret are printed by
`nes_headless ROM --misses`; `python -m recomp.suggest` turns them into hints.
"""
import argparse
import os
import sys
import tomllib
from collections import defaultdict

from .banks import assumed_layout, unit_windows, view, window_of
from .blocks import split_blocks
from .decoder import explore
from .emit_c import emit, fnv1a
from .ines import Cartridge


def explore_regions(cart, hints):
    """Decode every reachable region. Returns [(Program, 64 KB view,
    {window: bank})]: one region over $8000-$FFFF for mappers that never
    switch PRG, else one per (bank, window) so code never assumes a bank it
    cannot check."""
    layout = assumed_layout(cart)
    no_return = set(hints.get("no_return", []))
    if cart.mapper in (0, 3):
        entries = [v for v in cart.vectors() if v >= 0x8000] + list(hints.get("entries", []))
        entries += [a for _, a in hints.get("banked", [])]
        mem = view(cart, layout)
        return [(explore(mem, entries, no_return=no_return), mem, layout)]
    k = unit_windows(cart)
    seeds = set()

    def add(addr, bank=None):
        """Seed (bank of the unit's first window, addr)."""
        if addr < 0x8000:
            return  # RAM: interpreter only
        w = window_of(addr)
        if bank is None:
            if w not in layout:
                return  # bank unknown statically: left to the runtime / hints
            bank = layout[w]
        seeds.add((bank - w % k, addr))

    for v in cart.vectors():
        add(v)
    for a in hints.get("entries", []):
        add(a)
    for b, a in hints.get("banked", []):
        add(a, b)

    regions, done = {}, set()
    while seeds - done:
        new = seeds - done
        done |= new
        groups = defaultdict(set)
        for bank0, addr in new:
            groups[(bank0, window_of(addr) - window_of(addr) % k)].add(addr)
        for (bank0, w0), addrs in groups.items():
            banks = {w0 + i: bank0 + i for i in range(k)}
            prev = regions.get((bank0, w0))
            entries = (prev[0].entries if prev else set()) | addrs
            mem = prev[1] if prev else view(cart, {**layout, **banks})
            lo = 0x8000 + w0 * 0x2000
            hi = lo + k * 0x2000
            prog = explore(mem, entries, code_range=(lo, hi), no_return=no_return)
            regions[(bank0, w0)] = (prog, mem)
            for t in prog.external:
                if lo <= t < hi:
                    continue  # straddles the unit's end: interpreter
                add(t)
    return [(prog, mem, {w0 + i: bank0 + i for i in range(k)})
            for (bank0, w0), (prog, mem) in regions.items()]


def main(argv=None):
    ap = argparse.ArgumentParser(prog="python -m recomp", description=__doc__.split("\n")[1])
    ap.add_argument("rom")
    ap.add_argument("-o", "--output", required=True)
    ap.add_argument("--hints", help="per-game TOML with extra entries")
    args = ap.parse_args(argv)

    cart = Cartridge.load(args.rom)
    hints = {}
    if args.hints:
        with open(args.hints, "rb") as f:
            hints = tomllib.load(f)
    want = hints.get("prg_fnv1a")
    if want and want.upper() != f"{fnv1a(cart.prg):08X}":
        print(f"warning: {args.hints} was made for another dump (PRG {want}); "
              f"its entries may be wrong here (the runtime stays correct, misses go up)", file=sys.stderr)
    regions = explore_regions(cart, hints)
    compiled = [(split_blocks(prog), mem, banks) for prog, mem, banks in regions]
    src = emit(compiled, os.path.basename(args.rom), fnv1a(cart.prg), len(cart.prg) // 0x2000,
               banked=cart.mapper not in (0, 3))
    os.makedirs(os.path.dirname(os.path.abspath(args.output)), exist_ok=True)
    with open(args.output, "w", newline="\n") as f:
        f.write(src)
    ninstr = sum(len(p.instrs) for p, _, _ in regions)
    nblocks = sum(len(b) for b, _, _ in compiled)
    print(f"{os.path.basename(args.rom)}: {cart.summary()}")
    print(f"  {ninstr} instructions, {nblocks} blocks in {len(regions)} regions, "
          f"{sum(len(p.indirect) for p, _, _ in regions)} indirect jumps -> {args.output}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
