# 02 · Architecture

## Goals
1. Run NES and Famicom Disk System games as native executables built from
   recompiled 6502 code.
2. Be correct first: every layer is checked against an independent reference.
3. Keep the output readable: generated C, one function per basic block,
   named by bank and address (`b_07_C079`), so the binary also decompiles
   cleanly in IDA/Ghidra.

Non-goals for now: netplay.

## Components
```
              build time (all driven by CMake)            run time
ROM / .fds ─► nesrecomp (nes/recomp/, C) ─► gen/<game>.c ─┐
                decode, blocks, emit C, embed ROM         ├─► cc ─► <game>.exe
nes/games/<name>.toml (hints) ─┤   nes/runtime/, core/ ───┘          │
trace: nes_headless --play --misses (interpreter) ┘                   ▼
                     ┌──────────── nes/runtime (C11) ──────────────────┐
                     │ dispatch (nes_step): pc → block function        │
                     │   └─ none → cpu_step, the interpreter           │
                     │ bus (nes.c): RAM, PPU/APU registers, cartridge, │
                     │   controllers, DMA                              │
                     │ ppu.c, apu.c, cart.c (mappers), fds.c           │
                     ├─ nes/frontend.c: the NES as a system ───────────┤
                     │ core/frontend/: SDL3 window, audio, gamepads,   │
                     │   .ini, states, recordings (core/input_log.c)   │
                     └─────────────────────────────────────────────────┘
```

| Part | Language | Notes |
|------|----------|-------|
| `nes/recomp/` | C11 | `nesrecomp`, a build-time tool; also writes the interpreter's opcode switch |
| `nes/runtime/` | C11 | builds with clang/gcc/MSVC via CMake |
| `nes/frontend.c` | C11 + SDL3 | the NES behind `core/frontend/system.h` |
| `core/frontend/` | C11 + SDL3 (zlib) | shared by every system; SDL3 fetched by CMake or prebuilt; keyboard + gamepad |
| `core/` | C11 | input recordings and pad bits |
| `core/recomp/` | C11 | instruction templates and their PC markers, shared by nesrecomp and gbrecomp |
| `gb/` | C11 | the Game Boy: SM83 CPU, memory map, timer, PPU, cartridges, `gb` program; no sound yet (docs/05 "Game Boy") |
| `tests/` | Python `unittest` + C test runners | test ROMs downloaded by script, not committed; the build does not need Python |

## CPU state and memory
- `Cpu` (`nes/runtime/cpu.h`): registers, P in the real 6502 layout
  (`C=0x01 Z=0x02 I=0x04 D=0x08 B=0x10 1=0x20 V=0x40 N=0x80`, so values
  pushed by PHP/BRK/NMI and pulled by PLP/RTI are exactly as on hardware),
  the cycle count, interrupt lines and polling state.
- Every CPU cycle is one bus access. `rd`/`wr` (`cpu_ops.h`) use per-2 KB
  read/write maps for RAM, PRG RAM and ROM (inline loads while the lazy
  clock has room); everything else goes to `cpu_read`/`cpu_write`
  (`nes.c`). A bus that maps nothing (the CPU test bus) sees every access.
- The PPU and APU are caught up lazily: the bus counts master clocks and
  runs them in a batch when the CPU touches the chip, DMA runs, or the next
  event the chip raises by itself is due (vblank, frame IRQ, DMC fetch, a
  mapper IRQ), so every register access happens at the right dot.

## Recompiled code
- **Decoder** (`nes/recomp/analyze.c`): recursive descent from the reset/NMI/IRQ
  vectors plus entry points from `nes/games/<name>.toml` and traces.
  Instructions are keyed by address, so overlapping decodes are allowed.
- **Blocks** (`nes/recomp/emit.c`): one C function per basic block. Each
  instruction is the same C the interpreter's switch is generated from
  (`nes/recomp/c6502.c`), so bus traffic and cycle counts are identical by
  construction; the recompiler removes fetch/decode/dispatch, and code
  bytes and PC values become constants. A `switch (c->pc)` at the top makes
  every instruction an entry point (resume after an interrupt without
  falling back). After each instruction the block returns if an interrupt
  is due, so interrupts are taken on the same instruction as in the
  interpreter.
- **Chaining**: a block continues in the next one when it is known at
  compile time and in the same region (`RECOMP_CHAIN`, a guaranteed tail
  call with clang and gcc 15+; other compilers return to the dispatcher),
  unless an interrupt is due or `cpu.yield` is set (frame end; lockstep
  tests set it to compare after every block).
- **Identity**: the generated code carries an FNV-1a hash of the PRG; the
  runtime uses it only if the loaded ROM matches.
- **Embedded ROM**: the generated C also holds the whole iNES file, used when
  no ROM path is given. Not only the code bytes: data (tables, graphics) is
  read through pointers such as `LDA (ptr),Y` or `LDA table,X` whose range
  no static analysis can bound, so leaving bytes out could break a game
  late. A build therefore contains the game and must not be shared.
- **Banked mappers**: code is compiled per (PRG bank, window unit) and
  looked up by the bank mapped at run time, so a wrong static guess about
  banking costs coverage, never correctness. Units match how the mapper
  switches (8/16/32 KB); code never runs across units. After a store that
  may reach a mapper register, a block checks whether its own unit's bank
  changed and returns to the dispatcher if so.
- **Famicom Disk System**: the BIOS (8 KB at $E000) is recompiled like a
  ROM; blocks never chain into $E445, the disk check the automatic side
  swap hooks. A game's code is loaded from disk into RAM, so each file on
  the disk loaded from $0200 is its own region. Traces record RAM code with
  its first bytes, which tell the file. A block made from a disk file runs
  only while RAM holds the bytes it was made from: it checks on entry and
  after stores that may hit it, and returns without running if they
  differ, so the interpreter takes over (`recomp_rt.h`, `nes.c`
  `ram_lookup`).
- **Dispatch** (`nes_step`):
  ```
  if an interrupt is due or the CPU jammed: cpu_step   // interrupt entry
  f = lookup(pc, bank mapped at pc), else RAM code for pc (FDS)
  if f and it ran: done; else cpu_step                 // one instruction
  ```

## Speed notes
- Generated code calls one out-of-line copy of the bus helpers per file
  (`CPU_OPS_OUTLINE`): inlined everywhere, thousands of blocks outgrow the
  CPU caches and run slower than the interpreter.
- Recompiled CPU code is about 5% of the time; the PPU and APU are the rest.
  The PPU draws a visible line a tile (8 dots) at a time, with the 8
  palette indices of a background-only tile computed in one 64-bit word;
  the APU steps runs of cycles with no event at once. Both keep the output
  bit-identical (06).

## DMA
OAM DMA and DMC fetches halt the CPU on its next read; that read is the halt
cycle and is repeated on cycles the DMA unit does not use (`run_dma`). This
reproduces the hardware's extra $2007/$4016 reads during DMC fetches.

## Save states
The whole machine is the `Nes` struct (CPU, PPU, APU, cartridge registers,
RAM), plus CHR RAM and, for FDS, the disk sides. Recompiled code keeps no
state of its own (each block resumes from `c->pc` through its entry
switch), so interpreter and recompiled builds share one format: header
(magic, version, struct size, game hash), the struct, CHR RAM, disk.
Pointers (ROM data, bus, fast-path pages, linked code) are re-created on
load. Shortcut: the raw struct ties a state to one build layout; a
field-wise format is needed once states must survive program updates.

## Frontend
SDL3 window, audio and gamepads (`core/frontend/`), the same for every
system. A system plugs in through `core/frontend/system.h` (the NES:
`nes/frontend.c`): it loads the game and its saves, runs a frame (input
in, picture and sound out), saves and loads states, says why a game
halted, and takes the keys and settings that are its own (NES: F6, region,
overscan, sprite limit, FDS BIOS). The frontend does the rest. The emulator runs
on its own thread, so window moves and device changes do not stall the
sound. Settings come from `<program>.ini` next to the program
(`settings.c`). Scaling is done by the frontend (fit, integer or stretch),
so the optional TV frame (`tv_frame.c`, drawn procedurally at 4x and
filtered) and the picture (nearest-neighbour) scale together.

## The interpreter (`cpu_step`)
A switch-based 6502 core (`nes/runtime/cpu.c`, its switch generated by
`nesrecomp --cpu-switch` from the same C as the recompiled code), sharing
the `Cpu` struct and bus. It has three jobs:
1. Run anything the recompiler did not see (jump tables, RAM code, missed
   entries) and interrupt entry, and log those addresses so they can become
   hints.
2. Be the **oracle**: it is validated independently (nestest log, per-opcode
   test suites), and the recompiled code is then diffed against it in
   lockstep. This avoids testing the recompiler against a reference that
   shares its bugs.
3. Run whole games on its own (`build/nes`, or `--interp`).

## Missed code: the feedback loop
Static decoding cannot see every target. nestest alone uses RTS and RTI as
jumps, JMP through a RAM pointer, and runs code from RAM (see 06).
- The interpreter runs missed code; `nes_headless --misses` lists it;
  `nesrecomp --misses` keeps the chain starts (the first address of each
  run; the lowest one for loops entered from elsewhere) and merges them into
  the hints. Run without recompiled code, `--misses` lists every
  instruction executed, so one interpreter run is a complete trace of that
  session and no rebuild-and-retry rounds are needed.
- With `AUTO_HINTS` (on by default) the build does this itself with
  scripted input (`--play`) before recompiling. Your own play reaches
  further: recompiled games write `<program>.misses` next to themselves,
  and the next build reads it.
- A JIT for code found at run time stays a stretch goal: with the feedback
  loop, ROM code is above 99.9% recompiled in the test games.

## What the lockstep diff proves, and what it does not
The recompiler emits the interpreter's own C for each instruction (with
known operands made constant), so both share instruction semantics. A
lockstep match proves that translation, block discovery and bank handling
are right: the recompiled program does what the interpreter does. It cannot
catch an instruction both get wrong. That rests on the interpreter's own
checks: nestest, SingleStepTests (cycle-by-cycle bus traces) and the test
ROMs (rungs 2, 3 and 6 below). The templates (nes/recomp/c6502.c) never name
the PC in C: each use is a marker (operand fetch, read at the PC, its
value, jump, branch), which the interpreter's switch expands to run-time C
and the emitter to constants; a raw `c->pc` in a template stops the build,
and so do fetches that stop matching the instruction's size, so a template
change cannot silently read the wrong bytes.

## Verification ladder (each rung must pass before the next)
1. Opcode table matches an independent table (all 256 entries: mnemonic, mode,
   size, base cycles).
2. Interpreter matches `nestest.log` line by line (registers, P, SP, cycles).
3. Interpreter passes the per-opcode JSON tests (SingleStepTests, 2A03 set).
4. Recompiled nestest matches the same log.
5. Lockstep diff: a game runs recompiled and interpreted side by side;
   CPU state and memory are compared at every block boundary and the
   picture every frame; the first divergence is printed.
6. PPU/APU test ROMs (blargg and others; see 05) pass in the interpreter build.
7. Frame-hash regression: N frames of every recompiled game in the build,
   with scripted input, must match stored hashes (`tests/test_regression.py`).
