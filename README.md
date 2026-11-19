# retrocast

[![ci](https://github.com/Viper-Bit/retrocast/actions/workflows/ci.yml/badge.svg)](https://github.com/Viper-Bit/retrocast/actions/workflows/ci.yml)

A hobby, open-source **static recompiler for retro consoles**: it
translates a game's CPU code into C, compiles it into a native executable,
and runs it on a small runtime per console (picture, sound, cartridge
hardware, an SDL3 window and gamepads). A recompiled game can be run cycle
by cycle against a reference interpreter (`--diff`); every game tested that
way so far matches. Built for the challenge. No games included: bring your
own.

| System | Status | Details |
|---|---|---|
| NES and Famicom Disk System | 74 mappers (99.7% of licensed releases load), FDS, expansion sound, NTSC/PAL/Dendy; recompiled | [nes/README.md](nes/README.md) |
| Game Boy and Game Boy Color | ROM only, MBC1/2/3/5 (MBC3 clock), DMG and CGB mode; recompiled | [gb/README.md](gb/README.md) |

## How it works
1. Each system has a recompiler (`nes/recomp/` → `nesrecomp`, `gb/recomp/`
   → `gbrecomp`; C, built and run by CMake). It finds the reachable code and
   emits one C function per basic block, plus the ROM itself. Before that,
   the build plays the game for a while in the interpreter to find code
   static analysis cannot.
2. Each CPU's instructions are written once, as C templates (`core/recomp/`
   markers for the PC), and both the interpreter and the recompiled code
   are made from them: the same bus accesses, the same cycles.
3. Each system's runtime (`nes/runtime/`, `gb/runtime/`) provides the
   hardware; `core/frontend/` (SDL3) is the window, sound, gamepads,
   settings, save states and recordings for all of them.
4. Anything the recompiler did not see (jump tables, code in RAM) falls back
   to the **reference interpreter**, which is validated against public test
   suites and is also the oracle the recompiled code is diffed against.

Details: [docs/architecture.md](docs/architecture.md); plans:
[docs/roadmap.md](docs/roadmap.md).

## Downloads
[Releases](https://github.com/Viper-Bit/retrocast/releases) have builds for
Windows (x64, arm64), Linux (x64, arm64; glibc 2.35+) and macOS (arm64):
per system a program that plays a game in the interpreter (`nes`, `gb`), its
headless twin and its recompiler, and a **kit** that recompiles your games
into native programs with only CMake and a C compiler installed (SDL3 comes
prebuilt). `README.txt` in the archive explains both. Archives are made by
`tools/make_release.py` (`.github/workflows/release.yml`: push a tag `v*`
for a draft release).

## Build
Needs CMake ≥ 3.20 and a C11 compiler (clang/gcc/MSVC); any CMake generator
works (Visual Studio, Ninja, Makefiles). SDL3 is downloaded and built by
CMake, unless a prebuilt static SDL3 is found in `sdl3/` (or
`-DSDL3_PREFIX=...`). Python ≥ 3.11 is only needed for the tests.
```
cmake -S . -B build
cmake --build build --config Release
build/nes path/to/game.nes      # NES and FDS games, interpreted
build/gb path/to/game.gb        # Game Boy and Game Boy Color games, interpreted
```
Builds are **Release**; tests, timings and full-speed play assume it. Keep
`--config Release` with Visual Studio (the default generator with MSVC):
without it `cmake --build` builds Debug, which is too slow to play.
Ninja and Makefiles ignore the flag and build Release anyway. Programs are
put in `build/` with every generator (no `Release/` or `Debug/` subfolder).
Headless (no SDL3): add `-DFRONTEND=OFF`. Only some systems:
`-DSYSTEMS=gb` (default `nes;gb`). Warnings as errors: `-DWERROR=ON`. On Windows the game programs open
no console window; use the `_headless` programs for text output.

## Recompiling a game
```
cmake -S . -B build -DROMS="path/to/Game One.nes;path/to/Game Two (E) [!].gb"
cmake --build build --config Release   # -> build/game_one, build/game_two (+ _headless)
build/game_one                         # the ROM is embedded: no path needed
```
The file extension picks the system (`.nes`, `.fds`, `.gb`, `.gbc`). Before
recompiling, the build runs each game in the interpreter for
`AUTO_HINTS_FRAMES` (default 6000) frames of scripted play and recompiles
every address it ran (`-DAUTO_HINTS=OFF` skips this). More ways to reach
code, and disk games: see each system's README.

The whole ROM is embedded in the executable (graphics and data are read
through pointers no analysis can bound), so **do not share these builds**:
they contain the game. With `-DEMBED_ROM=OFF` the program asks for the ROM
file instead; the generated code still contains the game's code bytes, so
those builds are not for sharing either.

`build/game_one_headless --diff` runs the interpreter in lockstep and
reports the first difference; `--coverage` gives the share of CPU cycles in
recompiled code (add the scripted play of each system's README to get past
the title screen).

Paths and names: relative `ROMS` paths are relative to the folder cmake is
run from (else to the source folder) and are stored as absolute paths in
the cache; on Windows with Git Bash, give Windows paths (`pwd -W`). Target
names drop "(...)"/"[...]" tags from the file name.

## Controls and settings
Keyboard (player 1): arrows, X = A, Z = B, S = X, A = Y, Q = L, W = R,
Enter = Start, Right Shift = Select, F5 = reset, Esc = quit. Gamepads:
first = player 1, second = player 2, hot-plug; D-pad or left stick, East =
A, South = B, North = X, West = Y, shoulders = L and R, Back = Select,
Start. Each system reads the buttons its pad has (the NES and Game Boy:
A, B, Select, Start and the directions).

| Key | Action |
|-----|--------|
| F2 / F4 | save / load state (`ROM.state`; one slot) |
| F10 | TV frame on/off |
| F11 or Alt+Enter | fullscreen on/off |

Settings live in `<program>.ini` next to the program (`build/nes.ini`,
`build/game_one.ini`), written with defaults and comments on first run:
```
fullscreen = 0      # 1 = fullscreen at desktop resolution
scale = fit         # fit (keep aspect), integer (sharp pixels), stretch
tv_frame = 0        # 1 = picture inside a TV-like frame
aspect = tv         # tv = pixels the shape the console's screen gave them, square = square pixels
window_scale = 3    # starting window size
```
Each system adds its own keys and settings (its README).
- **Save states** work across builds of the same version: a state saved by
  the interpreter program loads in the recompiled one and the reverse.
- **Recordings**: `build/nes game.nes --record run.rcin`, then
  `--replay run.rcin` (live input resumes at the end); the same for `gb`. A
  recording names its system; one from another system is refused.
- A jammed CPU (a crash on hardware too) leaves the last picture on screen
  with a message; F5 resets, F4 loads a state.

## Tests
```
python tests/fetch_test_roms.py            # public test ROMs (also run by the tests on demand)
python tests/fetch_test_roms.py --sst      # optional: ~1 GB, per-opcode CPU tests (--sst-sm83: Game Boy)
python -m unittest discover -s tests -t .          # everything; one system: -s tests/gb
```
No Python packages needed. Tests are grouped by system (`tests/nes/`,
`tests/gb/`; shared helpers in `tests/`). CPU tests need the C build;
SingleStepTests files not downloaded are skipped (`NES_SST_REQUIRED=1`,
`GB_SST_REQUIRED=1` make them fail instead), and so are tests whose program
was not built (`RETROCAST_STRICT=1`: fail). Tests for recompiled games run
for the games the build was configured with and are skipped otherwise.

CI (`.github/workflows/ci.yml`) builds and tests only the systems a change
touches (`tools/ci_systems.py`; shared code means all of them) with GCC,
Clang and MSVC, warnings as errors, plus the window programs; test data is
kept in the Actions cache. The required check is `ci-ok`.

## Docs
| File | Content |
|------|---------|
| [docs/architecture.md](docs/architecture.md) | Layout, the pipeline, templates, recompiled code, frontend: what every system shares |
| [docs/roadmap.md](docs/roadmap.md) | The order rule, the systems, stretch goals |
| [docs/verification-log.md](docs/verification-log.md) | Checks of the shared parts |
| [docs/prior-art.md](docs/prior-art.md) | Other recompilers and emulators, and what we take from them |
| [docs/sources.md](docs/sources.md) | Every external source we used, with license notes |
| [nes/docs/](nes/docs/) | NES: architecture, roadmap, verification log, 6502 traps, timing constants, the first prototype |
| [gb/docs/](gb/docs/) | Game Boy: architecture, roadmap, verification log |

## ROMs
No ROMs, disk images or BIOS files are included or will ever be committed.
Bring your own legally dumped games. `roms/` is git-ignored.

## Credits
Written by the project author together with Claude (Anthropic) as a coding
partner; commits carry `Co-Authored-By` lines. Outside knowledge used along
the way is listed in [docs/sources.md](docs/sources.md).

## License
GPL-3.0-or-later (see [LICENSE](LICENSE)). Chosen so we can reuse code from
GPL emulators such as Mesen2 as well as MIT/zlib/BSD projects. Code copied or
adapted from another project must name its origin in the file header and in
[docs/sources.md](docs/sources.md).
