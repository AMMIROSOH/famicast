# CPS-1 roadmap

The order rule ([docs/roadmap.md](../../../docs/roadmap.md)): the reference
before the thing it checks. A milestone is done when its exit test passes.
The design: [docs/superpowers/specs/2026-11-23-cps1-design.md](../../../docs/superpowers/specs/2026-11-23-cps1-design.md).

| Milestone | Content | State |
|---|---|---|
| P1 | `core/zip`: directory, inflate, CRC | done |
| P2 | the 68000 machine, CPS-A/CPS-B picture, the game table with Final Fight | done |
| P3 | sound: Z80, YM2151, OKI MSM6295 | open |
| P4 | the window (`retrocast`, `cps1.ini`), CMake detection, save states | open |
| P5 | every plain-board game; stars, flip screen, graphics ROM mappers, raster interrupts | open |
| P6 | the recompiler (`cps1recomp`, `-DROMS`, `--diff`) | open |
| P7 | the Q-Sound games: dino, punisher, wof, slammasu, mbombrd | open |
| P8 | the rest: pang3, megaman, pnickj, qad, qtono2 | open |

## P1 · Zips
- [x] `core/zip.c`: the end record and central directory, stored and
  deflate entries, the CRC-32 check; no ZIP64.
- [x] **Exit:** `tests/test_zip.py`: zips Python writes (stored, levels 1
  and 9, empty to 1 MB, a back-reference 32 KB away) read back the same;
  cut and corrupted zips fail cleanly; every zip in the set reads as
  Python reads it.

## P2 · The machine and the picture
- [x] The 68000 map, inputs and DIP switches, the CPS-B ID port and
  multiplier, VBlank on level 2; the palette, three scroll layers,
  sprites and priority; the game table (Final Fight).
- [x] **Exit:** `cps1_tests`; Final Fight reaches its title in
  `cps1_headless --ppm`, checked by eye.
