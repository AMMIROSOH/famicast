# Genesis / Mega Drive verification log

What was checked, how, and the result. Newest first. A claim without a
reproducible check is not "verified".

Checks of shared parts: [docs/verification-log.md](../../docs/verification-log.md).

## 2026-11-22 · 68000 (C2)
- **SingleStepTests m68000** (`python tests/fetch_test_roms.py --sst-m68k`,
  pinned commit; `cpu/m68k/m68k_tests.c`, `tests/genesis/test_m68k.py`):
  each of the 317,500 tests runs one instruction and compares registers,
  SR, the next prefetch address, both queue words, RAM, and every bus
  transaction (kind, address, function code, strobes, data, clocks) with
  the idle spans between them. 125 of 127 families pass all 2,500 tests;
  TAS and TRAPV are compared by state only, as the corpus' README asks,
  and pass. MOVE.l passes 2,206: the rest are address errors in its write,
  where the flags MAME's microcode leaves are not modelled. Overall
  316,583 of 317,500 (99.7%); by state alone 317,206 (99.9%).
- **Found on the way**, from the corpus, each checked across all families:
  the pushed PC of an address error is the next prefetch address - 2, but
  - 4 after a displacement or index word, + 0 after a byte or word -(An)
  and in MOVEM, CMPM, ADDX/SUBX -(An), DBcc and UNLK; a byte or word (An)+
  steps even when its access faults, a long one does not; the frame's IR
  is the queue's, the next opcode once the last refill is done; CHK traps
  after 8 or 10 clocks by the sign of the microcode's s - d.
- **Builds**: MinGW gcc (all systems, `-DWERROR=ON`), WSL Linux gcc 13 and
  MSVC 2022 (`-DSYSTEMS=genesis -DWERROR=ON`), no warnings; MSVC's runner
  gives the same results. The strict suite passes (all systems).
- **Speed**: a loop of MOVE.l, ADD.l, ADDQ.l, MOVE.w (A0)+, DBF and BRA on a
  flat bus: 116 million instructions a second (MinGW gcc -O2), about 115
  times the real 7.67 MHz in clocks.
- **Not checked**: interrupts and trace (not in the corpus), clang (not
  installed here; CI builds with it), the template output for recompiled
  code (no 68000 recompiler yet).

## 2026-11-22 · Z80 (C1)
- **SingleStepTests z80** (pinned commit `ebe1875d`, MIT): all 1,604 files
  pass, 1,604,000 of 1,604,000 tests (`tests/genesis/test_z80.py`, runner
  `cpu/z80/z80_tests.c`, files converted by `fetch_sst_z80` in
  `tests/fetch_test_roms.py`). Per test it compares the registers (A to L,
  IX, IY, AF', BC', DE', HL', SP, PC, I, R, WZ, IM, IFF1, IFF2, EI, P, Q),
  every RAM byte, the port reads and writes in order, the total T-states,
  and each T-state's row of the bus: address, data and the read, write,
  memory and IO pins. The first run failed 7 files, all about Q: POP AF
  (it does not set Q) and SCF/CCF after DD or FD (the prefix counts as an
  instruction of its own, so Q reads 0). Nothing else needed a change.
- **The first converter** (files in `tests/roms/sst_z80` from before) dropped
  the cycles and the ports; the new files start with a magic and version
  and are fetched again when it is missing.
- **What the corpus has no files for**: ED opcodes outside 40-7F and the
  sixteen block instructions (they run as 8 T-state NOPs, R + 2), and chains
  of prefixes (DD DD, DD ED...: each extra prefix is a 4 T-state NOP). They
  follow the hardware as Sean Young describes it, not a test. HALT
  (4 T-state NOPs, PC stays), EI's one-instruction delay, IM 0, 1 and 2,
  NMI (11 T-states, to $66, IFF1 off, IFF2 kept, RETN restores it) and bit 7
  of R are checked by `z80_tests self` (`tests/genesis/test_z80.py`,
  `TestInterrupts`): interrupt timing 13 (IM 0 with RST, IM 1) and 19
  T-states (IM 2). They are not checked against hardware or another test.
- **Builds**: `-DSYSTEMS=genesis -DFRONTEND=OFF -DWERROR=ON` configures and
  builds clean with MinGW gcc 15, Linux gcc (WSL), clang (zig cc 0.17) and
  MSVC 2022. The corpus (1,604 of 1,604 files) and `self` pass with MinGW
  gcc, Linux gcc and MSVC; with clang `self` and three corpus files were
  run. The generated switch is byte for byte the same with gcc and clang.
