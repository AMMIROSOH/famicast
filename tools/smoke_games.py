"""Boot-smoke test over a ROM collection (local only; ROMs are never committed).

    python tools/smoke_games.py ROMDIR [--per-mapper 6] [--frames 1500] [--sheet out.png]

Picks good dumps ("[!]") per mapper, runs each in nes_headless with Start
presses, and reports: load errors, CPU jams, and whether the last frame has
content (more than 2 distinct colours). --sheet writes a contact sheet of the
last frames for a visual check. This finds crashes and black screens, not
subtle bugs: the test ROMs and --diff cover correctness.
"""
import argparse
import collections
import os
import re
import struct
import subprocess
import sys
import tempfile
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EXE = os.path.join(ROOT, "build", "nes_headless.exe" if os.name == "nt" else "nes_headless")
PRESSES = ["--press", "240:08", "--press", "480:08", "--press", "720:08", "--press", "900:01",
           "--press", "1100:08"]


def mapper_of(path):
    with open(path, "rb") as f:
        h = f.read(16)
    if h[:4] != b"NES\x1a":
        return None
    m = (h[6] >> 4) | (h[7] & 0xF0)
    return m | (h[8] & 0x0F) << 8 if (h[7] & 0x0C) == 0x08 else m  # NES 2.0: bits 8-11


def read_ppm(path):
    with open(path, "rb") as f:
        data = f.read()
    header, pixels = data.split(b"\n", 1)
    return pixels[:256 * 240 * 3]


def write_sheet(path, frames, cols=6, scale=1):
    w, h = 256, 240
    rows = (len(frames) + cols - 1) // cols
    W, H = cols * w, rows * h
    img = bytearray(W * H * 3)
    for k, px in enumerate(frames):
        ox, oy = (k % cols) * w, (k // cols) * h
        for y in range(h):
            dst = ((oy + y) * W + ox) * 3
            img[dst:dst + w * 3] = px[y * w * 3:(y + 1) * w * 3]
    raw = b"".join(b"\0" + bytes(img[y * W * 3:(y + 1) * W * 3]) for y in range(H))

    def chunk(t, b):
        return struct.pack(">I", len(b)) + t + b + struct.pack(">I", zlib.crc32(t + b))
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", W, H, 8, 2, 0, 0, 0)) +
                chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("romdir")
    ap.add_argument("--per-mapper", type=int, default=6)
    ap.add_argument("--frames", type=int, default=1500)
    ap.add_argument("--sheet")
    a = ap.parse_args()

    by_mapper = collections.defaultdict(list)
    for root, _, files in os.walk(a.romdir):
        for f in sorted(files):
            if f.lower().endswith(".nes") and "[!]" in f:
                p = os.path.join(root, f)
                by_mapper[mapper_of(p)].append(p)
    results, sheet = [], []
    with tempfile.TemporaryDirectory() as tmp:
        for m in sorted(k for k in by_mapper if k is not None):
            roms = by_mapper[m]
            step = max(1, len(roms) // a.per_mapper)
            for path in roms[::step][:a.per_mapper]:
                ppm = os.path.join(tmp, "f.ppm")
                r = subprocess.run([EXE, path, "--frames", str(a.frames), "--ppm", ppm, *PRESSES],
                                   capture_output=True, text=True, timeout=600)
                name = os.path.basename(path)
                if r.returncode == 2:
                    status = "unsupported" if "not supported" in r.stderr else "load error"
                elif r.returncode == 254:
                    status = "JAM " + r.stdout.strip()
                else:
                    px = read_ppm(ppm)
                    colours = len({px[i:i + 3] for i in range(0, len(px), 3 * 97)})
                    status = "ok" if colours > 2 else "blank"
                    if a.sheet and status != "unsupported":
                        sheet.append(px)
                results.append((m, status, name))
                print(f"{m:3} {status:12} {name}", flush=True)
    counts = collections.Counter(s.split()[0] for _, s, _ in results)
    print("summary:", dict(counts))
    if a.sheet and sheet:
        write_sheet(a.sheet, sheet)
        print("sheet:", a.sheet)
    return 0


if __name__ == "__main__":
    sys.exit(main())
