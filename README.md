# nes-recomp (working title)

A hobby, open-source **static recompiler for NES games**: it translates the
6502 code in a ROM into C, compiles it to a native executable, and runs it on a
small runtime (PPU, APU, mappers, SDL3 window/audio/gamepad).

Built for the challenge. First target: **Battle City** (mapper 0 / NROM).

## How it works (short version)
1. `recomp/` (`nesrecomp`, C, built and run by CMake) finds all reachable
   6502 code and emits one C function per basic block, plus the ROM itself.
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
real NES in both modes (M5b), with sound (M6). Mappers 0-4, 7, 11, 66 run in
the interpreter and recompile with bank-aware code (M7); one game per mapper
is verified identical to the interpreter. The recompiler is plain C and runs
inside the CMake build; it can trace a game first to find code by itself
(M8a). Save states, a coverage report, frame-hash regression tests and
display settings (fullscreen, scaling, TV frame) came with M8b. See
[docs/05-roadmap.md](docs/05-roadmap.md). The first prototype (hand-written
x86-64 output, in `legacy/`) was retired after it turned out to be incorrect;
see [docs/06-verification-log.md](docs/06-verification-log.md).

## Build
Needs CMake ≥ 3.20, a C11 compiler (clang/gcc/MSVC) and Ninja. SDL3 is
downloaded and built by CMake. Python ≥ 3.11 is only needed for the tests.
```
cmake -S . -B build -G Ninja
cmake --build build
build/nes path/to/game.nes      # any game on a supported mapper, interpreted
```
The build type defaults to **Release**; tests, timings and full-speed play
assume it. With a multi-config generator (Visual Studio), build with
`cmake --build build --config Release`. Headless (no SDL3): add
`-DNES_FRONTEND=OFF`. On Windows the game programs open no console window;
use the `_headless` programs for text output.

## Recompiling a game
```
cmake -S . -B build -DNES_GAMES="path/to/Battle City.nes;path/to/Metroid (E) [!].nes"
cmake --build build              # -> build/battle_city, build/metroid (+ _headless)
build/battle_city                # the ROM is embedded: no path needed
```
The whole ROM is embedded in the executable (graphics and data are read
through pointers no analysis can bound), so **do not share these builds**:
they contain the game.

Code the static decoder cannot find (jump tables, RTS used as a jump) runs in
the interpreter. Two ways to recompile it too:
- `-DNES_AUTO_HINTS=ON`: before recompiling, the build runs each game in the
  interpreter for `NES_AUTO_HINTS_FRAMES` (default 6000) frames of scripted
  play (`nes_headless --play 1`) and passes every address it ran to the
  recompiler. Merged hints are written to `build/gen/<name>.toml`; copy them
  to `games/` to keep them without tracing.
- By hand, from your own session (reaches what the script cannot):
  ```
  build/nes game.nes --record run.nesi
  build/nes_headless game.nes --replay run.nesi --misses --out misses.txt
  build/nesrecomp game.nes --hints games/battle_city.toml --misses misses.txt --save-hints games/battle_city.toml
  cmake --build build
  ```
`build/battle_city_headless --play 1 --diff --misses` runs the interpreter in
lockstep and reports the first difference, then what was still interpreted.
Target names drop "(...)"/"[...]" tags from the file name; hints live in
`games/<name>.toml` and record the PRG hash of the dump they were made for.
On Windows with Git Bash, give `NES_GAMES` Windows paths (`pwd -W`).

## Controls
Keyboard (player 1): arrows, X = A, Z = B, Enter = Start, Right Shift =
Select, F5 = reset, Esc = quit. Gamepads: first = player 1, second = player 2,
hot-plug; D-pad or left stick, East = A, South = B, Back = Select, Start.

| Key | Action |
|-----|--------|
| F2 / F4 | save / load state (`ROM.state`; one slot) |
| F10 | TV frame on/off |
| F11 or Alt+Enter | fullscreen on/off |

Display settings live in `<program>.ini` next to the program
(`build/battle_city.ini`, `build/nes.ini`), written with defaults and
comments on first run:
```
fullscreen = 0      # 1 = fullscreen at desktop resolution
scale = fit         # fit (keep aspect), integer (sharp pixels), stretch
tv_frame = 0        # 1 = picture inside a TV-like frame
window_scale = 3    # starting window size
```
Save states work across builds of the same version: a state saved by the
interpreter (`build/nes`) loads in the recompiled program and the reverse.

Record and replay input: `build/nes game.nes --record run.nesi`, then
`build/nes game.nes --replay run.nesi` (live input resumes at the end).

## Tests
```
python tests/fetch_test_roms.py --sst      # optional: ~1 GB download, per-opcode CPU tests
python -m unittest discover -s tests -t .
```
No Python packages needed. nestest is downloaded on first run (hash-checked).
CPU tests need the C build; SingleStepTests files not downloaded are skipped.
Tests for recompiled games (lockstep, save states across builds, frame
hashes in `tests/data/frame_hashes.json`) run for the games the build was
configured with and are skipped otherwise. After an intended output change:
`NES_UPDATE_HASHES=1 python -m unittest tests.test_regression`.

Coverage of a recompiled game: `build/battle_city_headless --play 1 --coverage`
(share of CPU cycles in recompiled code; `--misses` also lists the
addresses that were interpreted).

## Credits
Written by the project author together with Claude (Anthropic) as a coding
partner; commits carry `Co-Authored-By` lines. Outside knowledge used along
the way is listed in [docs/07-sources.md](docs/07-sources.md).

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
