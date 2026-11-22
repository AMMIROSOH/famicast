"""Contact sheet of Genesis games (local only; ROMs are never committed).

    python tools/genesis_sheet.py [SETDIR] [--every 40] [--frames 1200] [--sheet build/genesis_sheet.png]

Runs every Nth game of the licensed No-Intro set (.7z files, as
tests/genesis/test_boot.py picks them) in genesis_headless with Start
pressed now and then, and writes the last frames as one PNG, 320 x 240 per
game (H32 and 224-line pictures padded with black), for a look at whether
they reach title or play screens (systems/genesis/docs/roadmap.md D2).
"""
import argparse
import os
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from tests.genesis.test_boot import RUNNER, SET, sample  # noqa: E402
from tools.smoke_games import write_sheet  # noqa: E402

PRESSES = [a for f in (300, 500, 700, 900, 1000) for a in ("--press", "%d:08" % f)]  # Start


def frame(ppm):
    """The PPM's pixels padded to 320 x 240."""
    with open(ppm, "rb") as f:
        data = f.read()
    _, size, _, px = data.split(b"\n", 3)
    w, h = map(int, size.split())
    out = bytearray(320 * 240 * 3)
    for y in range(h):
        out[y * 960:y * 960 + w * 3] = px[y * w * 3:(y + 1) * w * 3]
    return bytes(out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("set", nargs="?", default=SET)
    ap.add_argument("--every", type=int, default=40)
    ap.add_argument("--frames", type=int, default=1200)
    ap.add_argument("--sheet", default=os.path.join(os.path.dirname(RUNNER), "genesis_sheet.png"))
    a = ap.parse_args()
    frames = []
    for game in sample(a.set, a.every):
        with tempfile.TemporaryDirectory() as d:
            subprocess.run(["7z", "x", "-y", "-o" + d, game], capture_output=True, check=True)
            rom = max((os.path.join(d, n) for n in os.listdir(d)), key=os.path.getsize)
            ppm = os.path.join(d, "f.ppm")
            r = subprocess.run([RUNNER, rom, "--frames", str(a.frames), "--ppm", ppm, *PRESSES],
                               capture_output=True, text=True, timeout=600)
            print("%-8s %s" % (r.stdout.split("result: ")[-1].strip(), os.path.basename(game)), flush=True)
            if os.path.exists(ppm):
                frames.append(frame(ppm))
    write_sheet(a.sheet, frames, cols=8, w=320, h=240)
    print("sheet:", a.sheet)


if __name__ == "__main__":
    main()
