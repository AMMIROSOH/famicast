# Roadmap

Order rule: **build the reference before the thing it checks.** For each
system the interpreter and its test harness come before the recompiler, so
the recompiler is never judged against itself. Each milestone has an exit
test; it is "done" only when that test passes in CI.

| System | Milestones | State |
|---|---|---|
| NES and Famicom Disk System | [M0-M11, speed passes](../nes/docs/roadmap.md) | done (a few test ROMs and boards open) |
| Game Boy and Game Boy Color | [G1-G6](../gb/docs/roadmap.md) | done (timing tests and RAM code open) |
| Next systems | SNES, GBA, Genesis, arcade: not started | open |

A new system follows the same steps: CPU templates and interpreter
against independent tests, the system and its test ROMs, the window
program behind `core/frontend/system.h`, then the recompiler and the
lockstep diff. Code moves to `core/` when a second system needs it.

## Stretch
- JIT for code in RAM or found at run time. With play-to-recompile and auto
  hints, ROM code is above 99.9% recompiled in the test games and FDS RAM
  code is recompiled per disk file, so a JIT has little left to gain.
- Analysis build: symbols per block for IDA/Ghidra, with a write-up.
- Hand-written x86-64 backend, checked against the C backend with `--diff`.
