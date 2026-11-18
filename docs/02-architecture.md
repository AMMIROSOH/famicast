# 02 · Architecture

## Goals
1. Run NES games as native executables built from recompiled 6502 code.
2. Be correct first: every layer is checked against an independent reference.
3. Keep the output readable: generated C, one function per block, named by
   address (`blk_C070`), so the binary also decompiles cleanly in IDA/Ghidra.

Non-goals for now: netplay. (PAL timing, expansion audio and the Famicom
Disk System, once listed here, are done: 04, 05; FDS games are interpreted.)

## Components
```
              build time (all driven by CMake)          run time
ROM ──► nesrecomp (recomp/, C) ──► gen/<game>.c ──┐
          decode, blocks, emit C, embed ROM       ├─► cc ──► <game>.exe
game.toml (hints) ─┤              runtime/ ───────┘          │
trace (optional) ──┘  nes_headless --play --misses (interpreter)
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
| `recomp/` | C11 | `nesrecomp`, a build-time tool; also writes the interpreter's opcode switch |
| `runtime/` | C11 | builds with clang/gcc/MSVC via CMake |
| platform | SDL3 (zlib) | fetched by CMake `FetchContent`; keyboard + gamepad |
| tests | Python `unittest` + C test runners | test ROMs downloaded by script, not committed; the build does not need Python |

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
- **Blocks** (`recomp/analyze.c`, `recomp/emit.c`): one C function per
  basic block, `static void b_C079(Cpu *c)`. Each instruction is the same C
  the interpreter's switch is generated from (`recomp/c6502.c`), so bus
  traffic and cycle counts are identical by construction; the recompiler
  removes fetch/decode/dispatch. A `switch (c->pc)` at the top makes every
  instruction start an entry point (resume after RTI without falling back).
  After each instruction the block returns if an interrupt is due, so
  interrupts happen on the same instruction as in the interpreter.
- **Identity**: the generated code carries an FNV-1a hash of the PRG; the
  runtime uses it only if the loaded ROM matches.
- **Embedded ROM**: the generated C also holds the whole iNES file, used when
  no ROM path is given. Not only the code bytes: data (tables, graphics) is
  read through pointers such as `LDA (ptr),Y` or `LDA table,X` whose range
  no static analysis can bound, so leaving bytes out could break a game
  late. A build therefore contains the game and must not be shared.
- **Banked mappers** (`recomp/analyze.c`, banking): code is compiled per (PRG bank,
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

## Speed notes
- Generated code calls one out-of-line copy of the bus helpers per file
  (`CPU_OPS_OUTLINE`): inlined everywhere, thousands of blocks outgrow the
  CPU caches and run slower than the interpreter.
- Blocks continue in the next block when it is known at compile time and in
  the same region (`RECOMP_CHAIN`, a guaranteed tail call), unless an
  interrupt is due or `cpu.yield` is set (frame end; lockstep tests set it
  to compare after every block).

## DMA
OAM DMA and DMC fetches halt the CPU on its next read; that read is the halt
cycle and is repeated on cycles the DMA unit does not use (`run_dma`). This
reproduces the hardware's extra $2007/$4016 reads during DMC fetches.

## Save states
The whole machine is the `Nes` struct (CPU, PPU, APU, cartridge registers,
RAM), plus CHR RAM. Recompiled code keeps no state of its own (each block
resumes from `c->pc` through its entry switch), so interpreter and
recompiled builds share one format: header (magic, version, struct size,
PRG hash), the struct, CHR RAM. Pointers (ROM data, bus, fast-path pages,
linked code) are re-created on load. Shortcut: the raw struct ties a state to
one build layout; a field-wise format is needed once states must survive
program updates.

## Frontend
SDL3 window, audio and gamepads (`runtime/platform/`). Display settings come
from `<program>.ini` next to the program (`settings.c`). Scaling is done by
the frontend (fit, integer or stretch), so the optional TV frame
(`tv_frame.c`, drawn procedurally at 4x and filtered) and the picture
(nearest-neighbour) scale together.

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
   lists it; `nesrecomp --misses` keeps the chain starts (the first address
   of each run; the lowest one for loops entered from elsewhere) and merges
   them into the hints. Run without recompiled code, `--misses` lists every
   instruction executed, so one interpreter run is a complete trace of that
   session and no rebuild-and-retry rounds are needed. With
   `NES_AUTO_HINTS` (on by default) the build does this itself with scripted input
   (`--play`) before recompiling (M8a). Coverage is limited to what the
   input reaches; your own play reaches further: recompiled games write
   `<program>.misses` next to themselves, and the next build reads it.
2. Later (stretch): a JIT for missed code, so targets found at run time get
   native speed without a rebuild. Not needed for speed in v1: a C 6502
   interpreter runs far faster than real time, and misses are rare after the
   feedback loop. Code in RAM always stays interpreted or JIT-compiled.

## What the lockstep diff proves, and what it does not
The recompiler emits the interpreter's own C for each instruction (with
known operands made constant, `recomp/emit.c`), so both share instruction
semantics. A lockstep match proves that translation, block discovery and
bank handling are right: the recompiled program does what the interpreter
does. It cannot catch an instruction both get wrong. That rests on the
interpreter's own checks: nestest, SingleStepTests (cycle-by-cycle bus
traces) and the test ROMs (rungs 2, 3 and 6 below). The emitter finds
operand fetches in that C by their text; it stops the build if an
instruction's fetches stop matching its size, so a template change cannot
silently read the wrong bytes.

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
