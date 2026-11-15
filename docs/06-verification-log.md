# 06 · Verification log

What was checked, how, and the result. Newest first. A claim without a
reproducible check is not "verified".

## 2026-11-15 · M5: recompiler v1
- **nestest, recompiled** (`nestest_recomp_tests`, hints in
  `tests/data/nestest.toml`): 7,404 instructions in 2,653 blocks. The CPU
  state after each of the 3,752 block steps equals the nestest.log line with
  the same cycle count; the run reaches the end of the log. Code at $0300
  (RAM) runs in the interpreter, as planned. First build had only 478
  instructions: nestest's automation entry $C000 is not a vector (reset is
  $C004), so it had to be a hint.
- **Battle City, recompiled**: 3,215 instructions found statically. 10
  minutes of scripted play (seed 1) showed 1,301 interpreted ROM addresses
  from the 8 indirect jumps; `recomp.suggest` reduced them to 28 entry points
  (`games/battle_city.toml`), after which: 4,924 instructions, 1,467 blocks,
  **0 interpreted instructions** in the same session (only the 35,656 NMI
  entries go through the interpreter, by design).
- **Lockstep** (`--diff`, 36,000 frames): CPU registers, cycle count, RAM,
  PRG RAM and PPU state identical after every block, frame images identical;
  final frame hash 9A346143 in both modes.
- **Speed**: interpreter 28.8 s, recompiled 37.4 s for the 10-minute session
  (Release, clang). Compiling the generated C with -Os/-O1 gives 32-35 s, so
  code size is not the main cost. Logged as M5b.
- Generated C compiles with -Wall -Wextra without warnings.

## 2026-11-15 · M4: input
- Recording format (`runtime/input_log.h`): "NESI", version 1, then pad 1,
  pad 2, flags per frame. Emulation is deterministic from power-on, so
  replays are exact.
- Battle City, 900 frames with Start/Start/Up/A presses: live run and replay
  give the same frame hash (5EBB8292); with no input the hash differs
  (F0AE1C10), so the presses reached the game through $4016.
- A recording written by Python (independent of the C writer) replays to the
  same hash as the `--press` run; a reset flag at frame 850 replays
  deterministically and changes the result.
- Gamepads: built against SDL3 3.4.20; no pad was connected at first.
  Later confirmed by the user with an Xbox Series X controller (detected as
  player 1, plays).

## 2026-11-15 · M3: PPU, bus, NROM
Run: `python -m unittest tests.test_ppu` (ROMs from the nes-test-roms mirror,
commit `95d8f621`). First run of the new PPU: 33/50 passed. After the fixes
below: **52/52**, and the CPU suites are still green.

| Suite | Result |
|---|---|
| ppu_vbl_nmi 01-10 | 10/10 |
| vbl_nmi_timing 1-4 | 4/4 |
| sprite_hit_tests_2005 01-11 | 11/11 (first try) |
| sprite_overflow_tests 1-5 | 5/5 |
| blargg_ppu_tests_2005 (palette_ram, sprite_ram, vbl_clear_time, vram_access) | 4/4 |
| ppu_open_bus, oam_read, oam_stress | 3/3 |
| instr_test-v5 singles 01-16 (CPU, in the full system) | 16/16 |
| test_ppu_read_buffer | pending, mapper 3 |

What it took:
- **Interrupt polling**: the CPU now samples NMI/IRQ after every cycle and
  acts on the sample from the instruction's second-to-last cycle (removes the
  M2 shortcut). Fixed 07-nmi_on_timing.
- **PPU dots per CPU cycle**: tried 0, 1, 2 and 3 of the 3 dots before the
  bus access. Only 2 passes 05-nmi_timing; kept 2 before, 1 after.
- **Vblank races** (nesdev "PPU frame timing"): $2002 read one dot before
  vblank → flag never set, no NMI; read on the set dot or the next → flag set,
  NMI dropped; NMI disabled in that window → no NMI. Fixed 02, 06, 08.
- **Odd-frame dot skip** uses the rendering-enable state of the previous dot.
  Fixed 10-even_odd_timing.
- **Open bus**: each latch bit decays after ~600 ms (36 frames) unless driven;
  $2002 drives bits 7-5 only, palette reads bits 5-0.
- **Sprite evaluation** per dot with the hardware overflow bug (m incremented
  with n). Fixed sprite_overflow 3.Timing and 4.Obscure.
- **Result locations**: the 2005 sprite tests keep the result at $F8 (checked:
  screen "FAILED: #5" ↔ $F8 = 5); the 2005 PPU tests use **$F0** (screen "$01"
  ↔ $F0 = 1).
- **$AB LXA disagreement**: instr_test-v5 (verified on a real NES by its
  author) needs A = X = operand; SingleStepTests expects (A | $EE) & operand
  (4,422/10,000 fail with ours). LXA is unstable on real chips; we follow the
  hardware-verified test and skip $AB in the SST test, with the reason.
- **Palette**: generated from the NTSC signal model; hue offset 3.9 chosen
  by checking $16 = red, $12 = blue, $1A = green, $2C = cyan.
- Battle City (interpreter): title screen at frame 400 and stage 1 after two
  Start presses (frames 300, 700) checked by screenshot.

## 2026-11-15 · M2: reference interpreter
Design: every `rd()`/`wr()` is one CPU cycle, including the dummy reads and
writes of real hardware, so cycle counts are not taken from a table. The
per-opcode C code is generated from `recomp/ops.py` by `recomp/c6502.py`
(the recompiler will reuse it); semantics helpers are in `runtime/cpu_ops.h`.
- **nestest**: `build/cpu_tests nestest` matches all 8,991 lines of
  `nestest.log` (PC, A, X, Y, P, SP and cycle count), including the
  unofficial-opcode section. Passed on the first run.
- **SingleStepTests** (`nes6502/v1`, 10,000 tests per opcode): registers,
  memory and the exact bus access of every cycle are compared. Result:
  **all 244 non-KIL opcodes pass 10,000/10,000**, including the unstable
  unofficial ones (XAA/LXA with magic 0xEE, SHA/SHX/SHY/TAS with the
  high-byte AND and page-cross address quirk). Full suite runs in ~1.5 s.
- `KIL` opcodes: registers now match SST (PC ends after the opcode; the
  first version rewound PC). SST also records 11 bus cycles of the jammed CPU;
  not modelled, those 12 opcodes are skipped in the test.
- Runner self-check: corrupting one expected value in a test file makes the
  runner report exactly that test as failed.
- Interrupts were first polled before each instruction (shortcut); replaced
  by cycle-exact polling in M3, still to be checked with `cpu_interrupts_v2`
  once the APU (IRQ source) exists.

## 2026-11-15 · M1: opcode table, iNES parser, decoder
Run: `python -m unittest discover -s tests -t .` → 10 tests OK.
- **Opcode table vs radare2** (`tests/test_ops.py`): 254/256 identical in
  mnemonic, mode and size. The two differences are radare2 errors, kept as a
  named list in the test so it fails if either side changes:
  - `$93` SHA/AHX: radare2 says `abs,X`, 3 bytes; it is `(zp),Y`, 2 bytes.
  - `$CB` AXS/SBX: radare2 prints it without `#`; it is immediate.
- Naming: nestest calls `$E3/$E7/...` **ISB** (not ISC); our table follows
  nestest so the logs compare directly.
- **iNES**: the prototype parser read the wrong header bytes (byte 5 as flags,
  mapper from the wrong bits) and only worked on Battle City by chance.
  Rewritten from the nesdev spec, tested with synthetic headers.
- **Decoder vs nestest.log** (`tests/test_decoder.py`): 7,045 instructions
  decoded. Transfers the decoder cannot predict, found by comparing the log
  with static successors: 600 RTS (all normal returns except one, PHA/PHA/RTS
  to $CE67), 2 RTI used as jumps ($CEAD→$CEAE, $CECD→$CECE), 2 JMP (ind)
  ($DB7B→$DB7E, $DBB5→$0300 in RAM). After feeding the ROM targets back as
  entries, every ROM instruction in the log matches in bytes, mnemonic and
  official/unofficial flag. No other kind of missed transfer exists.
- nestest files: identical ROM from qmtpro.com and the christopherpow mirror;
  the logs differ only in line endings (CRLF vs LF). Hashes pinned in
  `tests/fetch_test_roms.py`.
- Battle City first look: 3,215 instructions, 8 `JMP (ind)` sites, 3
  unofficial opcodes reached (to check: real or data decoded as code).

## 2026-11-15 · Audit of the first prototype (x86-64 asm backend)
Context: an earlier AI-driven session reported Battle City "running 60 frames
with zero halts". Re-checked by running the build and reading the generated
code.

**Result: the claim was false.** The CPU never got past reset. Observations:
- Stack pointer fell every frame (S = F4 → E1 → 1E, then wrapped) and PC was
  always `$DA9E`: an infinite loop inside the NMI handler (`X += $0E` until
  X = 4, with $0E = 0), with a new NMI nested on top each frame.
- "29,780 cycles per frame" was produced by the driver's own cycle limit, so
  it said nothing about the CPU.

Bugs found, in the order they blocked progress (each confirmed by patching a
scratch copy and re-running):
1. Driver fired NMI every frame regardless of PPUCTRL bit 7. Reset writes
   `$10` (NMI off), so the forced NMI was what produced the "boot".
2. `LDA $2002` was lifted to `nop`; driver loaded A but N/Z were never set, so
   the vblank wait (`BPL`) never exited.
3. `abs,X` / `abs,Y` used the word stored at the operand as a pointer.
   `STA $0400,X` cleared zero page instead of page 4.
4. `(zp),Y` decoded as a 3-byte instruction with a 16-bit operand (shared
   `ABSI` tag with `JMP (abs)`). Fixing it raised reachable code from 2,806 to
   3,175 instructions.
5. ADC carry used `seta` (> 0x100) instead of `>= 0x100` (109 sites).
6. ADC overflow written to bit 0 instead of V (0x40).
7. INC on memory stored the result at address = new value (address register
   reused for data).
8. Status bits swapped (Z=0x01, C=0x02) vs hardware (C=0x01, Z=0x02).
9. PPU model: $2005-$2007 treated as APU and dropped, $4014 ignored, $2003/$2006
   wrong, $2002 read did not clear vblank, only `LDA/STA abs` seen as I/O.

With 1-4 patched, reset ran further (RAM clear, VRAM fill) and then looped on
bugs 5 and 7. Decision: retire the asm backend, restart with a C backend and an
independently validated interpreter (see 02, 05).

## 2026-11-15 · Corrections to earlier entries
Earlier entries in this file (now removed) claimed `0x0A = PHX` on the NES and
listed an H flag. Both are wrong: the 2A03 has no 65C02 opcodes and the 6502
has no half-carry flag. `ops.py` in the prototype already had `$0A = ASL A`
correctly; the docs did not. The opcode table still has to be re-checked
against an independent source (roadmap M1); the earlier "0 mismatches" check
only covered 54 mnemonics.

## Still valid from earlier sessions
- Toolchain: clang/mingw at
  `%USERPROFILE%\.local\share\retcomm\toolchains\cmake-clang-v1\latest\bin`
  (x86_64-w64-mingw32-gcc/as, cmake 4.4). Local only; CI will use its own.
- Timing constants re-derived from nesdev (see 04).
