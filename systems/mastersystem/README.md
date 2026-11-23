# Sega Master System / Mark III

The Master System part of [retrocast](../../README.md): `retrocast` plays a
raw `.sms`, `sms_headless` runs one without a window, and `smsrecomp`
recompiles one (through `-DROMS`, see the
[root README](../../README.md#recompiling-a-game)).

## Status
- **CPU**: the shared Z80 (`cpu/z80`), which passes every SingleStepTests
  z80 file; this system links its own copy (`sms_` prefix) so it and the
  Genesis fit in one program.
- **Recompiled**: the code reached by a trace of scripted play and by your
  own play (`NAME.misses`), as C basic blocks; `--diff` compares every
  frame with the interpreter. Ten games ran 3,000 frames without a
  difference, 68-99.9% of their cycles recompiled; no faster than the
  interpreter, since drawing the picture costs far more than the Z80.
- **Cartridges**: Sega's mapper with cartridge RAM (kept in `game.sms.sav`),
  Codemasters' mapper; raw `.sms`, with or without a 512-byte copier
  header. No BIOS needed. Not yet: Korean and other mappers.
- **Picture**: the VDP's mode 4 at 192 lines: scrolling and its locks,
  flipped tiles, priority, 8x8, 8x16 and zoomed sprites, eight a line,
  frame and line interrupts. Not yet: the older TMS9918 modes, the SMS2's
  224- and 240-line modes, palette changes within a frame.
- **Sound**: the PSG, 44,100 Hz mono. Not yet: the FM sound unit.
- **Games**: of every 5th game in the No-Intro set, 124 of 137 reach a
  title screen or play ([verification log](docs/verification-log.md)).
- **Detection**: the "TMR SEGA" header with a Master System region; a game
  without one needs the `.sms` extension (or `--system sms`).

## Controls and settings
Button 1 = B (keyboard Z), button 2 = A (keyboard X), the console's Pause
button = Start (Enter); both pads. `mastersystem.ini` next to `retrocast`:
`region = auto` (PAL when the file name has `(Europe)`), `ntsc` or `pal`.

## Playing and testing
```
cmake -S . -B build -DSYSTEMS=mastersystem -DTESTS=ON
cmake --build build --config Release
build/retrocast path/to/game.sms
build/sms_headless path/to/game.sms --frames 600 --play --ppm last.ppm --wav sound.wav
python -m unittest discover -s tests/mastersystem -t .
```
The boot test plays four games from `roms/Sega - Master System - Mark III`
(or `MASTER_SYSTEM_ROMS_DIR`; .7z files, read with 7z on the PATH) and is
skipped without them.

## Docs
- [docs/roadmap.md](docs/roadmap.md): milestones S1-S3 and what is not modelled
- [docs/verification-log.md](docs/verification-log.md): what was checked, and how
