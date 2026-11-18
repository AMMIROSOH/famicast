# The first prototype (retired)

Before this project took its current shape, an earlier AI-driven session
built a different recompiler. It was audited on 2026-11-15, found not to
work, and retired. Its code lived in `legacy/` until 2026-11-18 and is kept
in git history:
```
git show 841141f:legacy/recomp/recomp.py      # one file
git checkout 841141f -- legacy                # the whole folder
```

## What it was
- `legacy/recomp/` (Python, ~1,300 lines): `ines.py` (header parser),
  `ops.py` (opcode table), `decoder.py` (reachable-code decoding and
  blocks), `codegen.py` (each block lifted to **x86-64 GAS assembly**),
  `recomp.py` (writes `generated.s`, `gen_blocks.h`, a C driver and a
  `build.bat`).
- `legacy/runtime/` (C, ~150 lines): a driver with 64 KB of flat memory, a
  stub PPU, and a frame loop that fired NMI and counted cycles.
- Tried on one game (Battle City) only.

## What it claimed, and what was true
The session reported the game "running 60 frames with zero halts". Running
the build and reading the generated code showed the CPU never got past
reset:
- The stack pointer fell every frame and PC was always `$DA9E`: an endless
  loop inside the NMI handler (`X += $0E` until X = 4, with $0E = 0), with a
  new NMI nested on top each frame.
- "29,780 cycles per frame" came from the driver's own cycle limit, so it
  said nothing about the CPU.

## Bugs found
In the order they blocked progress, each confirmed by patching a scratch
copy and running it again:
1. The driver fired NMI every frame regardless of PPUCTRL bit 7. Reset
   writes `$10` (NMI off), so the forced NMI was what produced the "boot".
2. `LDA $2002` was lifted to `nop`; the driver loaded A but N/Z were never
   set, so the vblank wait (`BPL`) never exited.
3. `abs,X` / `abs,Y` used the word stored at the operand as a pointer:
   `STA $0400,X` cleared zero page instead of page 4.
4. `(zp),Y` was decoded as a 3-byte instruction with a 16-bit operand (one
   tag shared with `JMP (abs)`). Fixing it raised the reachable code from
   2,806 to 3,175 instructions.
5. ADC carry used `seta` (> $100) instead of `>= $100` (109 sites).
6. ADC overflow was written to bit 0 instead of V ($40).
7. INC on memory stored the result at address = new value (the address
   register reused for data).
8. Status bits swapped (Z = $01, C = $02) against the hardware (C = $01,
   Z = $02).
9. The PPU model treated $2005-$2007 as APU registers and dropped them,
   ignored $4014, got $2003/$2006 wrong, did not clear vblank on a $2002
   read, and saw only `LDA/STA abs` as I/O.
10. The iNES parser read the wrong header bytes (byte 5 as flags, the
    mapper from the wrong bits) and worked on Battle City by chance.

With 1-4 patched, reset ran further (RAM clear, VRAM fill) and then looped
on bugs 5 and 7. Its docs also had errors (PHX/PLX on the 2A03, an H flag,
NMI masked by I, Mesen as MIT); see [prior art](../../docs/prior-art.md).

## Why it was retired, and what replaced it
Every bug above came from translating instructions by hand into another
language with nothing independent to compare against. The restart (02, 05)
reversed the order:
- **A reference first**: a C interpreter checked against nestest and the
  SingleStepTests before any recompiler existed.
- **One semantics**: the recompiler emits the interpreter's own C per
  instruction (`recomp/c6502.c`), so the two cannot disagree on what an
  instruction does.
- **Proof by comparison**: a recompiled game counts as working only when it
  matches the interpreter state by state (`--diff`), never because "it ran
  N frames".

The lessons are also listed in [6502 traps](6502-traps.md). Hand-written native
code generation is still on the roadmap's stretch list, but only checked
against the C backend with `--diff`.
