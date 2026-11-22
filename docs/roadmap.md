# Roadmap

Order rule: **build the reference before the thing it checks.** For each
system the interpreter and its test harness come before the recompiler, so
the recompiler is never judged against itself. Each milestone has an exit
test; it is "done" only when that test passes in CI.

| System | Milestones | State |
|---|---|---|
| NES and Famicom Disk System | [M0-M11, speed passes](../nes/docs/roadmap.md) | done (a few test ROMs and boards open) |
| Game Boy and Game Boy Color | [G1-G6](../gb/docs/roadmap.md) | done (timing tests and RAM code open) |
| Sega Genesis / Mega Drive | [C1-C2 (CPUs), D1-D5](../genesis/docs/roadmap.md) | done (public test ROMs and a console recording open) |
| Next systems | Master System, arcade (CPS-1, Neo Geo), SNES, GBA: not started | open |

A new system follows the same steps: CPU templates and interpreter
against independent tests, the system and its test ROMs, the window
program behind `core/frontend/system.h`, then the recompiler and the
lockstep diff. Code moves to `core/` when a second system needs it.
CPUs live in `cpu/<name>/`, one folder per CPU, shared by every system
with that CPU; the 68000 and the Z80 will go there
([cpu/README.md](../cpu/README.md)).

## Later
- One program for every system: a `retrocast` window program (the system
  picked from the file's extension, `--system` to override, `--headless`
  without a window) and a `retrocast-recomp` recompiler, in place of
  `nes`/`gb`, the headless programs and `nesrecomp`/`gbrecomp`. Each
  program now carries its own copy of SDL3 (about 2 MB of its 2.2).
  Needs `core/frontend/system.h` as a table per system instead of global
  functions; keep an SDL-free headless build for tests and CI. Decide once
  more systems are in: a system that does not fit one shared frontend
  keeps its own program.

## Stretch
- JIT for code in RAM or found at run time. With play-to-recompile and auto
  hints, ROM code is above 99.9% recompiled in the test games and FDS RAM
  code is recompiled per disk file, so a JIT has little left to gain.
- Analysis build: symbols per block for IDA/Ghidra, with a write-up.
- Hand-written x86-64 backend, checked against the C backend with `--diff`.
