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

from .decoder import decode_at
from .ines import Cartridge


def chain_starts(mem, misses):
    """Missed ROM addresses the decoder cannot reach from another miss
    (not its fall-through, not its static branch/jump target)."""
    follows = set()
    for pc in misses:
        ins = decode_at(mem, pc)
        if ins.falls_through:
            follows.add((pc + ins.size) & 0xFFFF)
        if ins.target is not None:
            follows.add(ins.target)
    return sorted(pc for pc in misses if pc not in follows)


def render(entries, no_return, found):
    lines = ["# Recompiler hints (see python -m recomp --help).",
             "# Entries marked 'runtime' came from python -m recomp.suggest.",
             "entries = ["]
    lines += [f"    0x{pc:04X},{'  # runtime' if pc in found else ''}" for pc in entries]
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

    mem = Cartridge.load(args.rom).nrom_image()
    with open(args.misses) as f:
        misses = {int(m.group(1), 16) for m in re.finditer(r"^MISS \$([0-9A-F]{4})", f.read(), re.M)}
    rom_misses = {pc for pc in misses if pc >= 0x8000}
    old = {}
    if args.hints and os.path.exists(args.hints):
        with open(args.hints, "rb") as f:
            old = tomllib.load(f)
    found = set(chain_starts(mem, rom_misses)) - set(old.get("entries", []))
    entries = sorted(set(old.get("entries", [])) | found)
    text = render(entries, old.get("no_return", []), found)
    print(f"{len(rom_misses)} ROM misses, {len(misses) - len(rom_misses)} RAM; "
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
