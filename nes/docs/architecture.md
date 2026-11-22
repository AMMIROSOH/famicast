# NES architecture

The NES and Famicom Disk System parts. What every system shares
(templates, blocks, chaining, states, frontend):
[docs/architecture.md](../../docs/architecture.md).

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

| Part | Notes |
|------|-------|
| `cpu/6502/` | the 6502, shared with other systems: opcode table (`ops.c`), C per opcode (`c6502.c`), the interpreter (`cpu6502.c`), its switch generator; see [cpu/README.md](../../cpu/README.md) |
| `nes/recomp/` | `nesrecomp`: `analyze.c` (decode, blocks, banks), `emit.c`, `main.c` |
| `nes/runtime/` | `nes.c` (bus, dispatch, states), `ppu.c`, `apu.c`, `cart.c` (mappers), `fds.c`, `palette.c`, `recomp_rt.h` |
| `nes/frontend.c` | the NES behind `core/frontend/system.h`: F6, region, overscan, sprite limit, FDS BIOS |
| `nes/games/` | per-game hints; `fds.toml`: the FDS BIOS's entry points |

## CPU state and memory
- `Cpu` (`cpu/6502/cpu6502.h`): registers, P in the real 6502 layout
  (`C=0x01 Z=0x02 I=0x04 D=0x08 B=0x10 1=0x20 V=0x40 N=0x80`, so values
  pushed by PHP/BRK/NMI and pulled by PLP/RTI are exactly as on hardware),
  the cycle count, interrupt lines and polling state.
- Every CPU cycle is one bus access. `rd`/`wr` (`cpu6502_ops.h`) use per-2 KB
  read/write maps for RAM, PRG RAM and ROM (inline loads while the lazy
  clock has room); everything else goes to `cpu_read`/`cpu_write`
  (`nes.c`). A bus that maps nothing (the CPU test bus) sees every access.
- The PPU and APU are caught up lazily: the bus counts master clocks and
  runs them in a batch when the CPU touches the chip, DMA runs, or the next
  event the chip raises by itself is due (vblank, frame IRQ, DMC fetch, a
  mapper IRQ), so every register access happens at the right dot.

## Recompiled code
- **Decoder** (`nes/recomp/analyze.c`): recursive descent from the
  reset/NMI/IRQ vectors plus entry points from `nes/games/<name>.toml` and
  traces. Instructions are keyed by address, so overlapping decodes are
  allowed.
- **Blocks** (`nes/recomp/emit.c`): as described in the shared
  architecture; `cpu.yield` (frame end) also stops chaining.
- **Banked mappers**: regions are (PRG bank, window unit). Units match how
  the mapper switches (8/16/32 KB); code never runs across units.
- **Famicom Disk System**: the BIOS (8 KB at $E000) is recompiled like a
  ROM; blocks never chain into $E445, the disk check the automatic side
  swap hooks. A game's code is loaded from disk into RAM, so each file on
  the disk loaded from $0200 is its own region. Traces record RAM code with
  its first bytes, which tell the file. A block made from a disk file runs
  only while RAM holds the bytes it was made from: it checks on entry and
  after stores that may hit it, and returns without running if they
  differ, so the interpreter takes over (`recomp_rt.h`, `nes.c`
  `ram_lookup`).
- **Hints and misses**: `nesrecomp --misses` keeps the chain starts of a
  trace (the first address of each run; the lowest one for loops entered
  from elsewhere) and merges them into the hints (`--save-hints`).
  Recompiled games write `<program>.misses` next to themselves when
  `record_misses` is set, and the next build reads it.
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
  bit-identical ([verification log](verification-log.md)).

## DMA
OAM DMA and DMC fetches halt the CPU on its next read; that read is the halt
cycle and is repeated on cycles the DMA unit does not use (`run_dma`). This
reproduces the hardware's extra $2007/$4016 reads during DMC fetches.

## Save states
The `Nes` struct (CPU, PPU, APU, cartridge registers, RAM), plus CHR RAM
and, for FDS, the disk sides; fast-path pages are re-created on load.

## Verification
nestest (log line by line, also recompiled), SingleStepTests 2A03, the
blargg and other test ROMs, lockstep diffs of games per mapper and frame
hashes: [verification-log.md](verification-log.md). The 6502 details that
are easy to get wrong: [6502-traps.md](6502-traps.md).
