# 06 · Verification log

What was checked, how, and the result. Newest first. A claim without a
reproducible check is not "verified".

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
