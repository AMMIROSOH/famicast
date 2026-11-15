"""Deterministic pseudo-random play session, written as a .nesi recording.

    python tests/play_script.py OUT.nesi [--frames N] [--seed S]

Starts a game (Start at frames 300 and 700, as for Battle City's title), then
holds random D-pad directions for 20-90 frames, taps A every few frames, and
presses Start twice every ~20 s (pause/unpause, or start a new game after a
game over). Used to drive coverage and lockstep tests without committing a
recording.
"""
import argparse
import random
import struct

A, START, UP, DOWN, LEFT, RIGHT = 0x01, 0x08, 0x10, 0x20, 0x40, 0x80


def frames(n, seed):
    rnd = random.Random(seed)
    pads = [0] * n
    for f in (300, 700):
        for i in range(f, min(f + 6, n)):
            pads[i] |= START
    f, direction = 760, 0
    while f < n:
        direction = rnd.choice([UP, DOWN, LEFT, RIGHT, 0])
        for i in range(f, min(f + rnd.randint(20, 90), n)):
            pads[i] |= direction
            if i % rnd.choice([6, 8, 12]) < 2:
                pads[i] |= A
        f = i + 1
    for f in range(1900, n, 1200):
        for start in (f, f + 90):
            for i in range(start, min(start + 6, n)):
                pads[i] = START
    return pads


def write(path, pads):
    with open(path, "wb") as out:
        out.write(b"NESI\x01")
        for p in pads:
            out.write(struct.pack("BBB", p, 0, 0))


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("out")
    ap.add_argument("--frames", type=int, default=36000)  # 10 minutes
    ap.add_argument("--seed", type=int, default=1)
    a = ap.parse_args()
    write(a.out, frames(a.frames, a.seed))
