# Capcom Play System I (CPS-1)

The CPS-1 part of [retrocast](../../README.md), its first arcade board.
A game is the zip of its chip dumps, as MAME and FBNeo sets have them
(`ffight.zip`); `cps1_headless` runs one without a window.

## Status
- **Games**: Final Fight (World). The others in the set follow by board
  group ([roadmap](docs/roadmap.md)).
- **Zips**: read by `core/zip.c` (our own inflate); a game is matched by the
  CRCs and sizes of its files, so names, PAL dumps and the clones of a
  merged set do not matter.
- **CPUs**: the shared 68000 (`cpu/m68k`), its own copy (`cps1_` prefix) so
  it and the Genesis fit in one program. Not yet: the Z80 and sound.
- **Picture**: the palette (six pages, uploaded on a palette-base write),
  scroll1-3 with flips and scroll2's row scroll, sprites of up to 16x16
  blocks, layer order and the priority masks. Not yet: stars, flip screen,
  the per-game graphics ROM mapper, raster interrupts.
- **Not yet**: sound, the window, save states, the recompiler.

## Running
`cps1_headless GAME.zip [--frames N] [--play] [--ppm FILE]`: `--play`
drops a coin and presses Start, then walks right; `--ppm` saves the last
frame.

## Docs
- [Roadmap](docs/roadmap.md)
- [Verification log](docs/verification-log.md)
