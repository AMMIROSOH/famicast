# 02 · Architecture

## Goals
1. Run NES games as native executables built from recompiled 6502 code.
2. Be correct first: every layer is checked against an independent reference.
3. Keep the output readable: generated C, one function per block, named by
   address (`blk_C070`), so the binary also decompiles cleanly in IDA/Ghidra.

Non-goals for now: PAL timing, Famicom Disk System, expansion audio, netplay.

## Components
```
              build time                               run time
ROM ──► recomp/ (Python) ──► gen/<game>.c ──┐
          decode, blocks, emit C            ├─► cc ──► <game>.exe
game.toml (hints) ──────────┘   runtime/ ───┘          │
                                                       ▼
                     ┌──────────── runtime (C11) ────────────┐
                     │ dispatch: pc → block function          │
                     │   └─ miss → interp6502 (fallback)      │
                     │ bus: RAM, PPU/APU regs, mapper, input  │
                     │ ppu, apu, mappers/, input              │
                     │ platform_sdl3: window, audio, gamepad  │
                     └────────────────────────────────────────┘
```

| Part | Language | Notes |
|------|----------|-------|
| `recomp/` | Python ≥ 3.11 | `tomllib` for configs; no third-party deps |
| `runtime/` | C11 | builds with clang/gcc/MSVC via CMake |
| platform | SDL3 (zlib) | fetched by CMake `FetchContent`; keyboard + gamepad |
| tests | Python `unittest` + a C test runner | test ROMs downloaded by script, not committed |

## CPU state and memory
```c
typedef struct { uint8_t a, x, y, s, p; uint16_t pc; uint64_t cycles; } Cpu;
```
- P uses the real 6502 layout: `C=0x01 Z=0x02 I=0x04 D=0x08 B=0x10 1=0x20 V=0x40 N=0x80`,
  so values pushed by PHP/BRK/NMI and pulled by PLP/RTI are exactly as on
  hardware.
- All memory access goes through `bus_read(addr)` / `bus_write(addr, v)`.
  The emitter may inline a direct array access only when the address is a
  constant known to be internal RAM ($0000-$07FF, mirrored) or fixed PRG-ROM.
- The PPU is caught up lazily: the bus counts pending dots and runs them in
  a batch when the CPU touches the PPU, OAM DMA runs, or vblank is due, so
  register reads/writes happen at the right dot without splitting blocks.
- `Cpu` carries per-2 KB read/write maps; RAM, PRG RAM and ROM accesses are
  inline loads while the lazy clock has room (`cpu_ops.h`). A bus that maps
  nothing (the CPU test bus) sees every access through `cpu_read/cpu_write`.

## Recompiled code
- **Decoder**: recursive descent from the reset/NMI/IRQ vectors plus extra
  entry points from `games/<name>.toml`. Instructions are keyed by address,
  so overlapping decodes are allowed.
- **Blocks** (`recomp/blocks.py`, `recomp/emit_c.py`): one C function per
  basic block, `static void b_C079(Cpu *c)`. Each instruction is the same C
  the interpreter's switch is generated from (`recomp/c6502.py`), so bus
  traffic and cycle counts are identical by construction; the recompiler
  removes fetch/decode/dispatch. A `switch (c->pc)` at the top makes every
  instruction start an entry point (resume after RTI without falling back).
  After each instruction the block returns if an interrupt is due, so
  interrupts happen on the same instruction as in the interpreter.
- **Identity**: the generated code carries an FNV-1a hash of the PRG; the
  runtime uses it only if the loaded ROM matches.
- **Banked mappers** (`recomp/banks.py`): code is compiled per (PRG bank,
  window unit) and looked up by the bank mapped at run time, so a wrong
  static guess about banking costs coverage, never correctness. Units match
  how the mapper switches (8/16/32 KB); code never runs across units. After
  a store that may reach a mapper register, a block checks whether its own
  unit's bank changed and returns to the dispatcher if so.
- **Dispatch loop** (runtime):
  ```
  loop:
      handle pending NMI/IRQ (push PC/P, jump to vector)
      f = lookup(c->pc, current bank)
      if f: f(c)  else: interp_step(c)   // fallback, one instruction
  ```
  Returning to the loop after each block keeps the C stack flat and gives an
  interrupt check point. Direct jumps between blocks (tail calls) are a later
  optimisation, only after the differential tests are green.
- **Interrupt granularity**: interrupts are taken between blocks. A block that
  writes $2000 or executes CLI/PLP/RTI ends there so a newly enabled interrupt is
  not delayed by a whole block. Exact per-instruction NMI timing is only needed
  by a few games; the interpreter fallback can provide it if one needs it.

## Fallback interpreter (`interp6502`)
A plain switch-based 6502 core sharing the same `Cpu` struct and bus. It has
three jobs:
1. Run anything the recompiler did not see (jump tables, RAM code, missed
   entries), and log those addresses so they can be added to `game.toml`.
2. Be the **oracle**: it is validated independently (nestest log, per-opcode
   test suites), and the recompiled code is then diffed against it in
   lockstep. This avoids the trap of testing the lifter against a reference
   that shares its bugs.
3. Run the whole game on its own (`--interp`), which gives a working emulator
   before the recompiler exists.

## Missed code: feedback loop now, JIT later
Static decoding cannot see every target. nestest alone uses RTS and RTI as
jumps, JMP through a RAM pointer, and runs code from RAM (see 06). Plan:
1. v1 (done): the interpreter runs missed code; `nes_headless --misses`
   lists it; `python -m recomp.suggest` keeps the chain starts and merges
   them into `games/<name>.toml`; recompiling then covers them. For Battle
   City one round (28 entries) brought a 10-minute session to zero misses.
2. Later (stretch): a JIT for missed code, so targets found at run time get
   native speed without a rebuild. Not needed for speed in v1: a C 6502
   interpreter runs far faster than real time, and misses are rare after the
   feedback loop. Code in RAM always stays interpreted or JIT-compiled.

## Verification ladder (each rung must pass before the next)
1. Opcode table matches an independent table (all 256 entries: mnemonic, mode,
   size, base cycles).
2. Interpreter matches `nestest.log` line by line (registers, P, SP, cycles).
3. Interpreter passes the per-opcode JSON tests (SingleStepTests, 2A03 set).
4. Recompiled nestest matches the same log.
5. Lockstep diff: game runs recompiled and interpreted side by side; compare
   CPU state + RAM hash at every block boundary; stop on first divergence and
   print the block, both states and the RAM delta.
6. PPU/APU test ROMs (blargg and others; see 05) pass in the interpreter build.
7. Frame-hash regression: N frames of Battle City with scripted input must
   match stored hashes.
