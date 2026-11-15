# nes-recomp (working title)

A hobby, open-source **static recompiler for NES games**: it translates the
6502 code in a ROM into C, compiles it to a native executable, and runs it on a
small runtime (PPU, APU, mappers, SDL3 window/audio/gamepad).

Built for the challenge. First target: **Battle City** (mapper 0 / NROM).

## How it works (short version)
1. `recomp/` (Python) finds all reachable 6502 code and emits one C function
   per basic block.
2. `runtime/` (C11) provides the hardware: CPU bus, PPU, APU, mappers, input.
3. Any address the recompiler did not see (jump tables, code in RAM) falls back
   to a **reference 6502 interpreter**, which is validated against public test
   suites and also serves as the oracle the recompiled code is diffed against.

See [docs/02-architecture.md](docs/02-architecture.md) and the plan in
[docs/05-roadmap.md](docs/05-roadmap.md).

## Status
M0-M3 done: verified opcode table and decoder, a reference interpreter (nestest,
SingleStepTests) and a PPU passing 52 blargg-style test ROMs. Battle City runs
on the interpreter and recompiled (M5: lockstep-identical to the interpreter
over 10 minutes of play, no interpreted code left), about 30x faster than a
real NES in both modes (M5b). Next: sound (M6). See
[docs/05-roadmap.md](docs/05-roadmap.md). The first prototype (hand-written
x86-64 output, in `legacy/`) was retired after it turned out to be incorrect;
see [docs/06-verification-log.md](docs/06-verification-log.md).

## Build
Needs CMake ≥ 3.20, a C11 compiler (clang/gcc/MSVC), Ninja, Python ≥ 3.11.
SDL3 is downloaded and built by CMake.
```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
build/nes path/to/game.nes      # NROM games for now
```
Headless (no SDL3): add `-DNES_FRONTEND=OFF`.

## Recompiling a game
```
cmake -S . -B build -DNES_GAMES="path/to/Battle City.nes"
cmake --build build              # -> build/battle_city(.exe) and battle_city_headless
build/battle_city "path/to/Battle City.nes"
```
The ROM is still needed at run time (graphics and data). Code the static
decoder missed runs in the interpreter; to recompile it too:
```
build/battle_city_headless game.nes --replay session.nesi --misses > misses.txt
python -m recomp.suggest game.nes misses.txt --hints games/battle_city.toml --write
cmake --build build
```
`--diff` runs the interpreter in lockstep and reports the first difference.

## Controls
Keyboard (player 1): arrows, X = A, Z = B, Enter = Start, Right Shift =
Select, F5 = reset, Esc = quit. Gamepads: first = player 1, second = player 2,
hot-plug; D-pad or left stick, East = A, South = B, Back = Select, Start.

Record and replay input: `build/nes game.nes --record run.nesi`, then
`build/nes game.nes --replay run.nesi` (live input resumes at the end).

## Tests
```
python tests/fetch_test_roms.py --sst      # optional: ~1 GB download, per-opcode CPU tests
python -m unittest discover -s tests -t .
```
No Python packages needed. nestest is downloaded on first run (hash-checked).
CPU tests need the C build; SingleStepTests files not downloaded are skipped.

## ROMs
No ROMs are included or will ever be committed. Bring your own legally dumped
cartridges. `roms/` is git-ignored.

## Docs
| File | Content |
|------|---------|
| [01-prior-art.md](docs/01-prior-art.md) | Other recompilers/emulators and what we take from them |
| [02-architecture.md](docs/02-architecture.md) | Components, data flow, design decisions |
| [03-6502-traps.md](docs/03-6502-traps.md) | 6502/2A03 details that are easy to get wrong |
| [04-timing-constants.md](docs/04-timing-constants.md) | NTSC clock and frame timing |
| [05-roadmap.md](docs/05-roadmap.md) | Milestones and exit criteria |
| [06-verification-log.md](docs/06-verification-log.md) | What was checked, how, and the result |
| [07-sources.md](docs/07-sources.md) | Every external source we used, with license notes |

## License
GPL-3.0-or-later (see [LICENSE](LICENSE)). Chosen so we can reuse code from
GPL emulators such as Mesen2 as well as MIT/zlib/BSD projects. Code copied or
adapted from another project must name its origin in the file header and in
[docs/07-sources.md](docs/07-sources.md).
