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
- **CPUs**: the shared 68000 and Z80 (`cpu/m68k`, `cpu/z80`), their own
  copies (`cps1_` prefix) so they and the Genesis and Master System fit in
  one program; the Z80 runs line by line after the 68000.
- **Sound**: the YM2151 (8 FM channels on the operator shared with the
  Genesis, `core/audio/fm.c`; DT1/DT2, the LFO, noise, timers and their IRQ)
  and the OKI MSM6295 (4 ADPCM voices, pin 7's two rates), mixed to mono
  at 55,930 Hz with MAME's CPS-1 weights. Checked on Final Fight: the
  YM2151 against ymfm (the same level and spectrum), the OKI bit-exact
  against MAME's, the mix against MAME's recording (verification log).
- **Picture**: the palette (six pages, uploaded on a palette-base write),
  scroll1-3 with flips and scroll2's row scroll, sprites of up to 16x16
  blocks, layer order and the priority masks. Not yet: stars, flip screen,
  the per-game graphics ROM mapper, raster interrupts.
- **Not yet**: the window, save states, the recompiler.

## Running
`cps1_headless GAME.zip [--frames N] [--play] [--ppm FILE] [--wav FILE]`:
`--play` drops a coin, presses Start on the title (frame 1,300), then walks
right; `--ppm` saves the last frame, `--wav` the sound.

## Docs
- [Roadmap](docs/roadmap.md)
- [Verification log](docs/verification-log.md)
