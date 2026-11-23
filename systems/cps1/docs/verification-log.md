# CPS-1 verification log

What was checked, how, and the result. Newest first. A claim without a
reproducible check is not "verified".

Checks of shared parts: [docs/verification-log.md](../../../docs/verification-log.md).

## 2026-11-23 · P1-P2 · zips, the machine and the picture
- **Zips** (`tests/test_zip.py`): stored and deflated entries Python
  writes read back with the same CRC and size; a cut zip and one with a
  flipped data byte exit with an error; all 32 zips of the set read as
  Python reads them.
- **Self-checks** (`cps1_tests`): load layouts (even/odd bytes, word
  swap, graphics interleave), extra zip entries ignored, a missing file
  named by CRC, a non-CPS-1 zip scoring 0; VBlank three times in three
  frames, inputs active low, the CPS-B ID and multiplier, unmapped reads;
  the palette's brightness rule, a tile of each scroll layer at its
  screen pixel, a flipped two-block sprite, a high-priority scroll2 pen
  over a sprite.
- **Final Fight** (`cps1_headless ffight.zip --frames 1200 --play --ppm`,
  checked by eye): the warning screen at frame 600, the title with
  "CREDIT =1" at 1,200, in the right colours. The CPS-B-04 palette
  control is at $2A (where the game writes $3F).
- **Detection**: `ffight.zip` is `cps1`; CPS-1 games not yet in the table
  and a CPS-2 zip are not.
- **Builds**: MinGW gcc with `-DWERROR=ON -DTESTS=ON`, every system; the
  Genesis tests pass on its 68000 copy.
- **Not checked**: sound, the window, other games.
