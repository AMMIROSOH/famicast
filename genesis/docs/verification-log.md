# Genesis / Mega Drive verification log

What was checked, how, and the result. Newest first. A claim without a
reproducible check is not "verified".

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
