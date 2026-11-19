# Verification log: shared parts

What was checked, how, and the result. Newest first. A claim without a
reproducible check is not "verified".

Checks of one system: [NES](../nes/docs/verification-log.md),
[Game Boy](../gb/docs/verification-log.md).

## 2026-11-19 · Frontend: sound channels, 16-bit buttons, recordings v2, 32-bit template addresses
- **Recordings**: `tests/nes/test_recording.py`, a made-up ROM reading pad 1
  through $4016: a version 1 and a version 2 recording of A + Start (with
  X, not on the NES pad) both read $90; `--record` writes `RCIN`, 2, `nes`
  and 5 bytes a frame; a `gb` recording is refused (exit 2, and by `nes`
  itself). `gb` records, replays its own recording.
- **Templates**: nesrecomp and gbrecomp output (nestest with its hints,
  cpu_instrs, both interpreter switches) byte for byte the same as before
  the address type changed.
- **Builds**: the suite passes with `RETROCAST_STRICT=1`; NES and Game Boy
  alone with gcc and `-Werror`; `nes` and `gb` (MSVC) run with a mono and a
  stereo audio device.

## 2026-11-18 · Layout: core/ and nes/, the frontend behind system.h
- **What**: the NES code moved to `nes/` (recomp/, runtime/, tests/,
  games/, its own CMakeLists.txt); `core/` holds what other systems will
  share: input recordings and pad bits, and the SDL3 frontend, which now
  reaches the console only through `core/frontend/system.h` (the NES:
  `nes/frontend.c`: loading, saves, frame, palette, region and overscan,
  F6, the jam message, misses). Settings the frontend does not know go to
  the system (`sys_setting`).
- **Same output**: the old (before the PC markers, 978c5b6) and the new
  nesrecomp, run with the same arguments on every 50th archive of a
  ~4,000-ROM NES set and every 200th FDS disk: 72 programs byte-identical,
  11 games on unsupported boards refused alike; `cpu_switch.inc` identical.
- **Correction** (PC markers, below): its "9 programs byte-identical" compared
  only 4 fresh files (the interpreter switch, nestest, the FDS BIOS and one
  game); the other 6 in `build/gen/` were left from earlier
  configurations. The comparison above covers that change too.
- **Frontend**: `nes` with a game and `fds` with a disk run 120/300 frames
  and exit cleanly; without a game, the usage line and exit code 2; a fresh
  `nes.ini` has the same lines as before (the NES keys now grouped after
  the general ones).
- **Release kit**: `tools/make_release.py` (Windows x64) stages `core/` and
  `nes/`; its kit check recompiles a generated ROM with the staged kit
  (window and headless program) and runs it: passes.
- **Tests**: as before (the 3 nestest tests could not download their log).

## 2026-11-18 · Instruction templates: PC markers instead of text matching
- **What**: c6502.c's templates name every use of the PC with a marker
  (`@F` operand fetch, `@R` read at the PC, `@P` its value, `@I` step,
  `@J` jump, `@B` taken branch); emit.c `expand` turns them into run-time
  C (interpreter switch, operands in the next window) or constants
  (recompiled blocks). Replaces `sub_pc`, which found `rd(c, c->pc++)` and
  `c->pc = ` by their text.
- **Same output**: `cpu_switch.inc` and the generated C of all 9 configured
  programs (8 games and the FDS BIOS) are byte-identical to before.
- **Mutations**: a literal `rd(c, c->pc++)` in a template stops nesrecomp
  ("c->pc in a template"); an absolute-mode template that skips its high
  byte fetch stops the build at the first such instruction. A high byte
  read with `@R` instead of `@F` is right in recompiled code (the PC is
  preset) and wrong in the interpreter, which the CPU tests catch.
- **Tests**: as before; the 3 nestest tests could not download their log
  (qmtpro.com's certificate has expired).

