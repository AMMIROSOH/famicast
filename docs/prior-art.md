# Prior art

What already exists and what we take from each. Links and licenses are in
[sources.md](sources.md). Entries marked *unverified* were not read
first-hand yet; check them before relying on the detail.

## NES static recompilers
- **jamulator** (Andrew Kelley, 2013, Go + LLVM, discontinued). The classic
  write-up. Its conclusion is the main lesson for us: purely static
  recompilation breaks on code that static disassembly cannot find (jump
  tables, data mixed with code, tricks like the `BIT` "skip next instruction"
  idiom in Super Mario Bros.). Kelley later said dynamic was "the way to go".
  → **We keep a runtime interpreter fallback**, so missed code still runs
  correctly instead of crashing.
- **"Solving BIT Magic"** (Alyssa Rosenzweig). Analysis of why jamulator's
  disassembler choice hurt it, using the SMB `BIT` trick. → Our decoder must
  allow overlapping instructions (one byte can start two different
  instructions).
- **NESRecomp** (mstan, github.com/mstan/nesrecomp). Active project that
  translates 6502 to C with a runner library that provides PPU/APU/mappers.
  Per-game config file (`game.toml`); used by ports such as Faxanadu, Dr. Mario,
  Mega Man 3. → Closest to our design; per-game hint files are a good idea.
  *Unverified:* internals, license, backend details.
- **NESgen** (Xenomega). Static NES ROM → C recompiler from a university
  compilers course. *Unverified:* scope and completeness.
- **SuperMarioBros-C** (Mitchell Sternke). Hand-assisted conversion of the SMB
  disassembly to C. → Shows what the C output of one game looks like when done
  well.

## Related recompilers on other systems
- **N64Recomp**. Static MIPS → C, with per-game configs and a runtime. → Same
  "emit C, let the C compiler do codegen" choice we made.

## Dynamic recompilation / emulators
- **FOSDEM 2026, "The joys and horrors of NES dynamic recompilation"**
  (Retrocomputing track; speaker listed as Alex Andreba, author of the toy
  dynarec *emunes*). Abstract not read yet.
- **Mesen2** (GPL-3.0). Accurate, readable emulator. → Reference for PPU, APU
  and mapper behavior, and the model for our FDS drive; under our GPL-3.0
  license we may adapt its code, with attribution. Also our second opinion
  when a game looks wrong.
- **FCEUX** (GPL). Second reference; check its exact GPL version before
  copying any code (GPL-2.0-only code cannot be combined with GPL-3.0).
- **RustyNES** (doublegate, GPL-3.0-or-later). Accurate interpreter in Rust,
  no JIT or recompiler; says it derives from Mesen2 and others. Read via a
  summary of its README and docs. Worth taking: the test harness design
  (`docs/testing-strategy.md`: nestest from $C000, blargg result protocol
  = poll $6000 until it leaves $80, $81 = press reset, text at $6004) and its
  timing model (`docs/scheduler.md`: one master clock, PPU caught up at each
  CPU half-cycle). Be careful with its accuracy numbers: they are self-reported
  and changed between releases, many "oracle" tests are snapshots of its own
  output, and the project says it is AI-assisted.

## Disassemblers used as an independent check
- **radare2** (LGPL-3.0), `libr/arch/p/6502` + `libr/arch/p/snes/optable.h`.
  Its 6502 decoder = own list of unofficial opcodes, falling back to the 65816
  table in 8-bit mode. We extract it into `tests/data/r2_6502.json`
  (`tools/import_r2_6502.py`) and compare our table to it. It agrees on
  254/256 opcodes; the two differences are radare2 mistakes (see 06).
  radare2's code analysis cannot resolve the jumps that really hurt static
  recompilation (RTS/RTI used as jumps, JMP through RAM pointers); those come
  from the runtime feedback loop ([architecture](architecture.md)).

## Corrections to the earlier version of this file
The first draft of these docs was produced by another AI model and contained
errors. Fixed here and in [the NES verification log](../nes/docs/verification-log.md):
- The 2A03 has **no** PHX/PHY/PLX/PLY. `$0A` is `ASL A`. (Those are 65C02
  opcodes.)
- The 6502 has **no** half-carry (H) flag.
- NMI is **not** masked by the I flag. It is enabled by PPUCTRL ($2000) bit 7.
- Mesen is GPL-3.0, not MIT.
- The FOSDEM talk was attributed to the wrong person.
