# Capcom Play System I (CPS-1)

The CPS-1 part of [retrocast](../../README.md), its first arcade board.
A game is the zip of its chip dumps, as MAME and FBNeo sets have them
(`ffight.zip`): `retrocast ffight.zip` plays it, `cps1_headless` runs one
without a window.

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
  blocks, layer order and the priority masks, the board's graphics ROM
  mapper (which tile codes each layer may use). Not yet: stars, flip
  screen, raster interrupts.
- **Detection**: a zip holding every file of a game in the table, by CRC
  and size, in `retrocast` and in the build (`cmake/detect_system.cmake`
  reads the table from `games.c`).
- **Save states**: F2 / F4 (`GAME.state` next to the zip), refused for
  another game or version.
- **Not yet**: the recompiler; DIP switch settings (their defaults).

## Controls and settings
Both players: buttons 1, 2, 3 on the pad's B, A and X (keyboard Z, X, S;
gamepad South, East, North), a coin on Select (Right Shift, gamepad Back),
Start. `cps1.ini` next to `retrocast` has no CPS-1 settings yet.

## Running
`retrocast GAME.zip` plays it. `cps1_headless GAME.zip [--frames N]
[--play] [--ppm FILE] [--wav FILE] [--state-at N FILE] [--load-state FILE]`:
`--play` drops a coin, presses Start on the title (frame 1,300), then walks
right; `--ppm` saves the last frame, `--wav` the sound; a loaded state goes
on from its frame.

## Docs
- [Roadmap](docs/roadmap.md)
- [Verification log](docs/verification-log.md)
