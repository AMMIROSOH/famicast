# Genesis / Mega Drive roadmap

The order rule ([docs/roadmap.md](../../docs/roadmap.md)): the reference
before the thing it checks. The two CPUs come first, each against its
public single-step corpus, then the machine around them, then the
recompiler. A milestone is done when its exit test passes in CI.

| Milestone | Content | State |
|---|---|---|
| [C1](#c1--z80-cpuz80) | Z80 templates and interpreter (`cpu/z80`) | done |
| [C2](#c2--68000-cpum68k) | 68000 templates and interpreter (`cpu/m68k`) | done (MOVE.l address-error flags open) |
| [D1](#d1--the-machine) | memory map, I/O, Z80 bus arbitration, cartridges | open |
| [D2](#d2--vdp) | the VDP: planes, window, sprites, DMA, interrupts | open |
| [D3](#d3--window-program) | the window program, pads, save states | open |
| [D4](#d4--sound) | SN76489 and YM2612 | open |
| [D5](#d5--recompiler) | the 68000 recompiler, lockstep `--diff` | open |

## The hardware
Facts the milestones build on (NTSC; PAL where it differs). Sources:
Sega's Genesis technical overview and software manual as reproduced on
plutiedev.com and the SpritesMind / Sega Retro wikis; each milestone
checks what it uses against a test ROM or the single-step corpora, and
corrects this list where they disagree.

- **Clocks**: master clock 53.693175 MHz (PAL 53.203424). 68000 =
  master / 7 (7.67 MHz), Z80 = master / 15 (3.58 MHz). The YM2612 runs
  at master / 7, the SN76489 (inside the VDP) at master / 15.
- **68000 memory map**:
  - `$000000-$3FFFFF` cartridge ROM, linear (no bank register; the few
    larger games use a mapper at `$A130F3-$A130FF`). Battery RAM, when the
    header says so, is switched in over ROM by `$A130F1`.
  - `$400000-$7FFFFF` expansion (Mega-CD): not emulated.
  - `$A00000-$A0FFFF` the Z80's space as the 68000 sees it (only while
    it holds the Z80 bus).
  - `$A10000-$A1001F` I/O: version register `$A10001`, pad data
    `$A10003/5/7`, pad control `$A10009/B/D`.
  - `$A11100` Z80 bus request, `$A11200` Z80 reset; `$A14000` TMSS
    (later models: the game writes "SEGA" there or the VDP stays locked).
  - `$C00000-$C0001F` VDP: data `$C00000`, control `$C00004`, H/V
    counter `$C00008`, PSG `$C00011`.
  - `$E00000-$FFFFFF` work RAM, 64 KB, mirrored (games use `$FF0000`).
- **Z80 memory map**: `$0000-$1FFF` 8 KB RAM (mirrored at `$2000`),
  `$4000-$4003` YM2612, `$6000` bank register (9 bits, written one bit at
  a time), `$7F11` PSG, `$7F00-$7F1F` VDP, `$8000-$FFFF` a 32 KB window
  into the 68000's space at the bank.
- **Interrupts** (68000 autovectors): vertical blank level 6, horizontal
  (line counter) level 4, external (pad port) level 2. The Z80 gets its
  interrupt (IM 1) at vertical blank.
- **VDP**: 64 KB VRAM, 64 colours of 9 bits in CRAM (4 palettes of 16),
  40 words of vertical scroll RAM, 24 registers. Two scrolling planes (A,
  B), a window plane, sprites; one priority bit per tile and per sprite;
  shadow/highlight mode. Widths 256 (H32) or 320 (H40) pixels, 224 lines
  (240 on PAL in V30). Sprites: 80 in H40 (64 in H32), 20 per line (16),
  each 1-4 by 1-4 tiles. DMA: 68000 memory to VRAM, CRAM or VSRAM; VRAM
  fill; VRAM copy; the 68000 stops while it copies from its memory.
  262 lines a frame (PAL 313).

## C1 · Z80 (cpu/z80)
- [x] Templates for every opcode, documented and undocumented (CB, DD,
  ED, FD, DDCB, FDCB tables), written once as for the SM83; the
  interpreter's switch generated from them (`z80_gen`); names prefixed
  `z80_` (cpu/README.md).
- [x] Interrupts (IM 0/1/2, NMI), HALT, the R register, the undocumented
  flags (bits 3 and 5, from WZ where the hardware takes them).
- [x] **Exit:** SingleStepTests z80 (pinned commit), every file: registers,
  flags, RAM, I/O ports, and the bus cycle by cycle.

## C2 · 68000 (cpu/m68k)
- [x] Templates per instruction and addressing mode, written once
  (`cpu/m68k/m68k.c`, markers `@W` `@N` `@Q` for the prefetch queue's
  words); the interpreter's 1,541 handlers and its 64K table generated
  from them (`m68k_gen`); the two-word prefetch queue; names prefixed
  `m68k_`.
- [x] Exceptions: address errors (the 7-word frame, the PC the microcode
  pushes, which register steps stand), illegal and privileged
  instructions, line A/F, TRAP, TRAPV, CHK, divide by zero, trace,
  interrupts by level (the last two not in the corpus). Bus errors are not
  raised (no system needs them yet).
- [x] **Exit:** SingleStepTests m68000 (pinned commit): 125 of 127
  families pass every test on state and on every bus transaction with its
  clocks; TAS and TRAPV by state only (the corpus' README: their logs are
  not reliable), both pass.
- [ ] Open: MOVE.l, 2,206 of 2,500: on an address error in its write the
  flags it leaves depend on how far the microcode got (unchanged, V and C
  cleared, or N and Z of the first word written), not yet modelled.

## D1 · The machine
- [ ] The memory maps above, the I/O ports (3- and 6-button pads), the
  version register, Z80 bus request and reset, the Z80 bank, cartridge
  ROM and battery RAM from the header, TMSS.
- [ ] Both CPUs on one master-clock timeline.
- [ ] **Exit:** public test ROMs for the memory map and the CPUs on the
  machine (to be chosen, pinned with their hash, licence checked); a
  sample of the licensed set boots to its first VDP writes.

## D2 · VDP
- [ ] Registers, the data and control ports, VRAM/CRAM/VSRAM, the three
  DMA kinds with their timing, the H/V counter, the interrupts.
- [ ] Rendering: planes A and B with full and per-cell or per-line
  scrolling, the window, sprites with their limits, priorities,
  shadow/highlight; H32/H40.
- [ ] **Exit:** public VDP test ROMs pixel for pixel (to be chosen and
  pinned); a contact sheet of a sample of the licensed set reaches title
  or play screens.

## D3 · Window program
- [ ] `genesis` behind `core/frontend/system.h`: picture, pads (3 and 6
  buttons on the 16-bit input), save states, recordings.

## D4 · Sound
- [ ] SN76489, then YM2612 (6 FM channels, channel 6 as DAC), stereo.
- [ ] **Exit:** test ROMs where they exist; the output of a sample
  checked against a reference recording.

## D5 · Recompiler
- [ ] 68000 code from the cartridge recompiled (regions, the trace from
  the interpreter, hints and play misses as on the other systems); the
  Z80 runs from its RAM (the sound driver the 68000 copies there), so it
  stays interpreted.
- [ ] **Exit:** `genesis_headless --diff` in lockstep on a sample, with
  the share of 68000 cycles recompiled.

## Later
- The Master System and Game Gear: the same Z80, a small machine, a
  cheap way to check the Z80 recompiler on ROM code.
- Arcade boards with the 68000 and Z80 (CPS-1, Neo Geo).
