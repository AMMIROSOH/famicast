"""Speed benchmark: best of N runs of a scripted session (timings on a
desktop vary by ±20% between runs, so the minimum is the useful number).

    python tools/bench.py ROM EXE [EXE...] [--frames 6000] [--runs 3]
"""
import argparse
import os
import re
import subprocess

ap = argparse.ArgumentParser()
ap.add_argument("rom")
ap.add_argument("exes", nargs="+")
ap.add_argument("--frames", type=int, default=6000)
ap.add_argument("--runs", type=int, default=3)
a = ap.parse_args()

for exe in a.exes:
    times, hashes = [], set()
    for _ in range(a.runs):
        out = subprocess.run([os.path.abspath(exe), a.rom, "--play", "1", "--frames", str(a.frames),
                              "--time", "--hash"], capture_output=True, text=True).stdout
        times.append(float(re.search(r"TIME ([\d.]+) s", out).group(1)))
        hashes.add(re.search(r"HASH (\w+)", out).group(1))
    best = min(times)
    print(f"{os.path.basename(exe):28} best {best:6.2f} s  {a.frames / 60.0988 / best:5.1f}x real time"
          f"  hash {','.join(sorted(hashes))}")
