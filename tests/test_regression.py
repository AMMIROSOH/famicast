"""Frame-hash regression suite: each recompiled game built here (-DROMS,
ROM embedded) plays the same scripted session; the last frame, CPU cycle
count and audio must hash to the recorded values (tests/data/frame_hashes.json).

Hashes are kept per game name and PRG hash, so a different dump of a game is
skipped, not failed. After an intended change in output (a PPU or APU fix):
    NES_UPDATE_HASHES=1 python -m unittest tests.test_regression
and say in nes/docs/verification-log.md why the hashes changed.
"""
import json
import os
import re
import subprocess
import unittest

from tests.games import ROOT
from tests.nesrecomp import BUILD, EXE

DATA = os.path.join(ROOT, "tests", "data", "frame_hashes.json")
FRAMES = 3000


def built_games():
    """{name: PRG hash} of the games this build recompiles (-DROMS)."""
    games = {}
    listing = os.path.join(BUILD, "nes_games.txt")
    names = open(listing).read().split() if os.path.exists(listing) else []
    for name in names:
        path = os.path.join(BUILD, "gen", name + ".c")
        if not os.path.exists(path) or not os.path.exists(os.path.join(BUILD, name + "_headless" + EXE)):
            continue  # configured but not built yet
        with open(path) as f:
            games[name] = re.search(r"PRG hash ([0-9A-F]{8})", f.read(400)).group(1)
    return games


def play(name):
    out = subprocess.run([os.path.join(BUILD, name + "_headless" + EXE), "--play", "1", "--frames", str(FRAMES),
                          "--hash", "--audio-hash"], capture_output=True, text=True, timeout=600).stdout
    frame = re.search(r"^HASH (\w+) cycles=(\d+)", out, re.M)
    audio = re.search(r"^AUDIOHASH (\w+)", out, re.M)
    return {"frame": frame.group(1), "cycles": int(frame.group(2)), "audio": audio.group(1)}


class TestFrameHashes(unittest.TestCase):
    def test_games(self):
        games = built_games()
        if not games:
            self.skipTest("no recompiled games in the build (-DROMS=...)")
        known = {}
        if os.path.exists(DATA):
            with open(DATA) as f:
                known = json.load(f)
        update = os.environ.get("NES_UPDATE_HASHES") == "1"
        for name, prg in sorted(games.items()):
            with self.subTest(game=name):
                want = known.get(name)
                if not update and (not want or want["prg_fnv1a"] != prg):
                    continue  # not recorded, or another dump of the game
                got = {"prg_fnv1a": prg, "frames": FRAMES, **play(name)}
                if update:
                    known[name] = got
                else:
                    self.assertEqual(got, want)
        if update:
            with open(DATA, "w", newline="\n") as f:
                json.dump(known, f, indent=2, sort_keys=True)
                f.write("\n")


if __name__ == "__main__":
    unittest.main()
