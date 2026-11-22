# Sega Genesis / Mega Drive

The Genesis part of [retrocast](../README.md). Not playable yet: the two
CPUs come first, each checked against its public single-step corpus,
before the machine is built around them.

## Status
- **Z80** (`cpu/z80`, the sound CPU): done (roadmap C1). Templates for
  every opcode (documented and undocumented), interpreter, interrupts;
  all 1,604 SingleStepTests z80 files pass: 1,604,000 tests, state, ports
  and every T-state of the bus.
- **68000** (`cpu/m68k`, the main CPU): the interpreter passes
  SingleStepTests m68000 on 125 of its 127 instruction families, every bus
  transaction with its clocks included (99.7% of the 317,500 tests); MOVE.l
  misses the flags of some address errors (roadmap C2).
- **The machine** (memory map, VDP, sound, window program, recompiler):
  not started.

## Docs
| File | Content |
|------|---------|
| [roadmap.md](docs/roadmap.md) | The hardware facts the work builds on, milestones C1-C2 and D1-D5 |
| [verification-log.md](docs/verification-log.md) | What was checked, how, and the result |
