# Roadmap

Order rule: **build the reference before the thing it checks.** For each
system the interpreter and its test harness come before the recompiler, so
the recompiler is never judged against itself. Each milestone has an exit
test; it is "done" only when that test passes in CI.

| System | Milestones | State |
|---|---|---|
| NES and Famicom Disk System | [M0-M11, speed passes](../systems/nes/docs/roadmap.md) | done (a few test ROMs and boards open) |
| Game Boy and Game Boy Color | [G1-G6](../systems/gb/docs/roadmap.md) | done (timing tests and RAM code open) |
| Sega Genesis / Mega Drive | [C1-C2 (CPUs), D1-D5](../systems/genesis/docs/roadmap.md) | done (public test ROMs and a console recording open) |
| Atari 2600 | [A1-A6](../systems/atari2600/docs/roadmap.md) | done (cartridge schemes, TIA quirks and test ROMs open) |
| Next systems | Master System, arcade (CPS-1, Neo Geo), SNES, GBA: not started | open |

A new system follows the same steps: CPU templates and interpreter
against independent tests, the system and its test ROMs, the window
program behind `core/frontend/system.h`, then the recompiler and the
lockstep diff. Code moves to `core/` when a second system needs it.
CPUs live in `cpu/<name>/`, one folder per CPU, shared by every system
with that CPU (the list: [cpu/README.md](../cpu/README.md)).

## Later
- **One program for every system: done for playing.** `retrocast` plays a
  game of any system, picked from the file's bytes (`core/detect.h`,
  `--system` to override); it replaced the `nes`, `gb`, `genesis` and
  `atari2600` window programs (one copy of SDL3 instead of four). The
  headless programs stay one per system: SDL-free, and the tests and CI
  run them.
- **One recompiler** (`retrocast-recomp` in place of `nesrecomp`,
  `gbrecomp`, `mdrecomp`, `atari2600recomp`): open. The four `main.c`
  share names (`fnv1a`, `explore`, `emit_program`, `Region`, ...) and the
  6502's templates are unprefixed (`Op`, `OPS`, `body`): each recompiler
  needs its names made static or prefixed, then one `main` picks the
  system as `retrocast` does. They are build-time tools, one per system
  in the release, so this saves little.

## Stretch
- JIT for code in RAM or found at run time. With play-to-recompile and auto
  hints, ROM code is above 99.9% recompiled in the test games and FDS RAM
  code is recompiled per disk file, so a JIT has little left to gain.
- Analysis build: symbols per block for IDA/Ghidra, with a write-up.
- Hand-written x86-64 backend, checked against the C backend with `--diff`.
