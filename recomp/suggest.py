"""Turn runtime fallback addresses into recompiler hints (docs/02 feedback loop).

    nes_headless ROM --replay run.nesi --misses > misses.txt
    python -m recomp.suggest ROM misses.txt --hints games/NAME.toml [--write]

Each run of interpreted instructions needs only its first address as an entry;
the decoder follows the rest. RAM addresses are skipped (always interpreted).
Prints the merged entry list, or rewrites the hints file with --write.
"""
import argparse
import os
import re
import sys
import tomllib

from .banks import assumed_layout, unit_windows, view, window_of
from .decoder import decode_at
from .emit_c import fnv1a
from .ines import Cartridge


def chain_starts(mem, misses):
    """Missed ROM addresses the decoder cannot reach from another miss
    (not its fall-through, not its static branch/jump target)."""
    edges = {}
    for pc in misses:
        ins = decode_at(mem, pc)
        succ = set()
        if ins.falls_through:
            succ.add((pc + ins.size) & 0xFFFF)
        if ins.target is not None:
            succ.add(ins.target)
        edges[pc] = succ & set(misses)
    follows = set().union(*edges.values()) if edges else set()
    starts = {pc for pc in misses if pc not in follows}
    # A loop entered from code the decoder cannot see (RAM, another bank) has
    # no start of its own: take its lowest address until all is reachable.
    while True:
        seen, work = set(), list(starts)
        while work:
            pc = work.pop()
            if pc not in seen:
                seen.add(pc)
                work += edges.get(pc, ())
        rest = set(misses) - seen
        if not rest:
            return sorted(starts)
        starts.add(min(rest))


def render(entries, banked, no_return, found, prg_hash):
    lines = ["# Recompiler hints (see python -m recomp --help).",
             "# Entries marked 'runtime' came from python -m recomp.suggest.",
             f'prg_fnv1a = "{prg_hash:08X}"  # the dump these hints belong to',
             "entries = ["]
    lines += [f"    0x{pc:04X}," for pc in entries]
    lines.append("]")
    lines.append("banked = [  # [8 KB PRG bank, address]")
    lines += [f"    [{b}, 0x{pc:04X}],{'  # runtime' if (b, pc) in found else ''}" for b, pc in banked]
    lines.append("]")
    if no_return:
        lines.append("no_return = [" + ", ".join(f"0x{pc:04X}" for pc in no_return) + "]")
    return "\n".join(lines) + "\n"


def main(argv=None):
    ap = argparse.ArgumentParser(prog="python -m recomp.suggest")
    ap.add_argument("rom")
    ap.add_argument("misses", help="output of nes_headless --misses")
    ap.add_argument("--hints", help="existing hints TOML to merge with")
    ap.add_argument("--write", action="store_true", help="rewrite the hints file")
    args = ap.parse_args(argv)

    cart = Cartridge.load(args.rom)
    layout = assumed_layout(cart)
    with open(args.misses) as f:
        text = f.read()
    rom_misses = {(int(b), int(a, 16)) for a, b in
                  re.findall(r"^MISS \$([0-9A-F]{4}) \d+ bank (\d+)", text, re.M)}
    ram_misses = re.findall(r"^MISS \$[0-9A-F]{4} \d+ \(RAM\)", text, re.M)
    old = {}
    if args.hints and os.path.exists(args.hints):
        with open(args.hints, "rb") as f:
            old = tomllib.load(f)
    known = {tuple(x) for x in old.get("banked", [])}
    found = set()
    by_region = {}
    for b, a in rom_misses:
        by_region.setdefault((b, window_of(a)), set()).add(a)
    k = unit_windows(cart)
    for (b, w), addrs in by_region.items():
        w0, b0 = w - w % k, b - w % k
        mem = view(cart, {**layout, **{w0 + i: b0 + i for i in range(k)}})
        found |= {(b, a) for a in chain_starts(mem, addrs)}
    found -= known
    banked = sorted(known | found)
    text = render(old.get("entries", []), banked, old.get("no_return", []), found, fnv1a(cart.prg))
    print(f"{len(rom_misses)} ROM misses, {len(ram_misses)} RAM; "
          f"{len(found)} new entries", file=sys.stderr)
    if args.write and args.hints:
        os.makedirs(os.path.dirname(os.path.abspath(args.hints)), exist_ok=True)
        with open(args.hints, "w", newline="\n") as f:
            f.write(text)
    else:
        sys.stdout.write(text)
    return 0


if __name__ == "__main__":
    sys.exit(main())
