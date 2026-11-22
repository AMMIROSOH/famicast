# Genesis / Mega Drive verification log

What was checked, how, and the result. Newest first. A claim without a
reproducible check is not "verified".

Checks of shared parts: [docs/verification-log.md](../../../docs/verification-log.md).

## 2026-11-23 · 68000 MOVE.L address errors (C2)
- **SingleStepTests m68000** (`cpu/m68k/m68k_tests.c`, the same pinned
  corpus as below): MOVE.l now passes 2,500 of 2,500, and no other family
  regressed — all 127 families pass, 317,500 tests on state, every bus
  transaction with its clocks and strobes (TAS and TRAPV by state only).
- **Two causes**: the address-error frame pushed the queue's IR after the
  last refill instead of the opcode (`c->op`, as `m68k_cpu.h` says it is
  for), which cost MOVE.l 294 tests on the frame's IR and status word; and
  a long MOVE's flags at a write fault were set from the full long too
  early. The flags the microcode has reached depend on the source and
  destination: register or immediate source, no flags yet to (An)/(An)+
  and only N/Z of the first word to d(An)/d(An,Xn); memory source, the low
  word's to (An)/(An)+ and absolute long, the full long's elsewhere. The
  final long's flags are set after the write.
- **The one exception**: a word MOVE to -(An) that faults on its write
  reports the shifted queue IR in the frame, as the corpus records; the
  generic case uses the opcode. Checked by comparing every address-error
  frame's IR against both the opcode and the queue IR at the fault, across
  all families.
- **Builds**: gcc with `-Wall` and MSVC with `-DWERROR=ON`, no warnings;
  the generated copy in `build/gen` is remade by `m68k_gen` from the
  edited template. The Genesis suite passes with `RETROCAST_STRICT=1` and
  `GENESIS_SST_REQUIRED=1`, the recompiler's lockstep test included.

## 2026-11-22 · The recompiler (D5)
- **Lengths**: `m68k_ext_words` against every template without a branch,
  at build time (`m68k_gen` stops on a mismatch): 39,853 opcodes agree;
  15 spot checks by hand (MOVE.L #,abs.L, JSR abs.L, DBcc, MOVEM, CMPI.W
  #,abs.L, BTST #,abs.L...).
- **Lockstep** (`NAME_headless --diff`: the CPUs, the Z80, work and Z80 RAM
  after every line, the whole machine after every frame): every 200th game
  of the licensed set, 3,000 frames each with Start and A pressed now and
  then, traced first for 3,000 frames (AUTO_HINTS): 007 Shitou, Championship
  Bowling, FIFA 98, Jungle Strike, Mystical Fighter, R.B.I. Baseball 3,
  Splatterhouse 3, Turbo OutRun: no difference; 94.33, 98.81, 97.59, 96.71,
  99.88, 99.97, 99.97 and 95.87% (007 Shitou) of the 68000's clocks in
  recompiled code. Sonic & Knuckles: 2,000 frames, no difference, 98.53%.
- **CI** (`tests/genesis/test_recomp.py`): a ROM of random legal
  instructions with random extension words, conditional skips and
  checkpoints (every exception resumes at the next one); the interpreter
  runs 12,927 distinct instructions of it in 120 frames; lockstep with no
  difference, 100% recompiled. A planted bug (the address-error PC
  adjustment not reset per instruction) is caught at frame 0, line 14.
- **The program**: `mdtest_random` plays its embedded ROM without a path.
- **Speed**: Sonic & Knuckles, 3,000 frames: 3.30 s recompiled, 3.47 s
  interpreted (gcc -O2). The profile: sound 32%, the Z80 17%, the VDP 8%;
  the 68000 was not the bottleneck.
- **Build**: the 8 games recompile and build with `-DWERROR=ON` (gcc 15,
  `musttail` chaining) in 2 min 46 s on 8 jobs; generated files 3-18 MB
  (the embedded ROM is most of it). Not built here: clang, MSVC (CI).

## 2026-11-22 · Sound (D4)
- **Self-checks** (`systems/genesis/tests/md_tests.c`, known answers): a PSG tone
  of period $100 measures within 0.5 Hz of master / 15 / 32 / 256
  (436.96 Hz); a lone YM2612 operator (algorithm 7, block 4, F-number
  1000) within 0.5 Hz of 1000 x 2^3 x 53,267 / 2^20 (406.4 Hz), its peak a
  third of full scale (one channel at full); after key off with release
  rate 15 the envelope reaches 1023 and the output is silent; timer A
  ($27 = $05, TA = 924) sets its flag on the 100th sample, not the 99th,
  and $27 bit 4 clears it; the status reads through $A04000; the DAC at
  $FF gives +1/3.
- **Games** (`genesis_headless --wav`, 1,500 frames): Sonic & Knuckles,
  Phantasy Star II, Ghouls'n Ghosts make sound from their Z80 or 68000
  drivers, nothing clipped. Their music against the A440 grid (4 s FFT
  windows, the 6 strongest peaks, 84 per game): Sonic & Knuckles a median
  of +2.3 cents off its nearest note; Phantasy Star II +25.9 and Ghouls'n
  Ghosts +25.7, both with a spread (IQR) of 2.5 cents: one offset for
  every note, so the games' own tuning, not this code (a wrong clock would
  move Sonic & Knuckles too; wrong F-number or block handling would
  scatter the notes). Before the DC block, Phantasy Star II gave a flat
  -1/3 for 5 s: its DAC is on at 0 before the driver writes $80.
- **The whole set** (`--boot`): still 1,556 of 1,566 with the Z80's
  window wait.
- **Speed**: 3,000 frames of Sonic & Knuckles with sound in 3.5 s (gcc
  -O2), about 14 times real time.
- **Not checked**: against a console recording (none at hand), SSG-EG
  games, the PSG's loudness against the FM, the DAC's pitch in games
  (the window wait is Genesis Plus GX's figure).

## 2026-11-22 · The window program (D3)
- **Self-checks** (`systems/genesis/tests/md_tests.c`): a state saved, two frames
  run, the state loaded and the two frames run again give the same work
  RAM, picture and 68000 clocks; a state does not load into another game;
  the reset button restarts the 68000 from its vectors with work RAM kept
  and the Z80 held.
- **The program** (`build/genesis`, Linux, SDL's offscreen video and dummy
  audio drivers): Sonic & Knuckles for 300 frames with `--record`, 5.1 s
  (59.9 frames a second, the NTSC rate); the recording ("RCIN" version 2,
  system "md") replays; Phantasy Star II writes its 16 KB battery save
  (`ROM.sav`); `genesis.ini` gets the `pad` setting.
- **Not checked**: a real window and gamepad on this machine (no look at
  the program's own window: the pictures were checked through
  `genesis_headless --ppm`), Windows and macOS builds, PAL games' aspect.

## 2026-11-22 · The VDP (D2)
- **Self-checks** (`systems/genesis/tests/md_tests.c`): VRAM through the data
  port with the increment, CRAM's 9 bits, a DMA from work RAM to VRAM
  (and the 68000's wait), a plane A tile and a sprite on screen at the
  right pixels in H40 V28, 2 vertical and 450 horizontal interrupts in 2
  frames (one every line, 0-224), the 68000 resuming after STOP, the
  status' FIFO and PAL bits. They check the code against the facts it was
  written from, not against a console.
- **Contact sheets** (`tools/genesis_sheet.py`, 1,200 frames, Start
  pressed now and then; every 40th and every 37th game of the licensed
  set; and 13 picked for shadow/highlight, the window and line scrolling:
  Sonic 2 and 3, Gunstar Heroes, Vectorman, Castlevania Bloodlines,
  Thunder Force IV, Streets of Rage 2, Shining Force II, Phantasy Star IV,
  Mortal Kombat II, Golden Axe, Altered Beast, Kid Chameleon): looked at
  one by one. Every game shows its title, a menu, its intro or play, with
  the right colours, text, sprites and layers; the few black frames were
  fades (checked at other frames: Castlevania, Mortal Kombat II, Bonanza
  Bros., Power Rangers). Not a pixel check: no reference pictures.
- **The whole set** (`genesis_headless --boot`): 1,556 of 1,566 reach
  their first VDP write (one more than in D1).
- **Found on the way**: an interrupt that ends a STOP pushed the STOP's
  own address, so the 68000 went back to STOP for ever (Thunder Force IV
  waited with its display off). STOP leaves its immediate word in the
  queue, so the return address is pc, not pc - 4 (`cpu/m68k/m68k_cpu.c`);
  checked by `md_tests` (the count stays 0 without the fix). Interrupts
  are not in SingleStepTests m68000, so its results cannot change.
- **Builds**: Linux gcc, all systems, `-DWERROR=ON`. Not built here:
  clang, MSVC (CI builds both).
- **Not checked**: public VDP test ROMs (none with a licence we could
  check; roadmap D2), mid-line changes, DMA and FIFO timing, the H counter,
  colour levels, interlace.

## 2026-11-22 · The machine (D1)
- **Self-checks** (`systems/genesis/tests/md_tests.c`, `tests/genesis/test_machine.py`):
  a ROM made by the test, the 68000 looping on BRA.S, the 68000's bus
  poked directly. Version register per header region (letters and hex
  digit), work RAM and its mirror, Z80 RAM and its mirror, bus request
  and its grant bit, Z80 reset; a Z80 program reads ROM through bank 0
  and work RAM through bank $1FE (nine bit writes to $6000) and counts its
  vertical blank interrupts (3 in 3 frames); 3-button reads for TH 1 and
  0; the 6-button sequence (the mark on the third TH low, X Y Z after it,
  back to 3 buttons after the fourth rise, reset after the timeout);
  battery RAM from the header and its write protect; TMSS: no lock on a
  version 0 console, a lock before "SEGA" and none after on a version 1
  one. These check the code against the hardware facts in roadmap.md,
  not against a console: the Z80's INT length (BlastEm's measured 171.5
  Z80 clocks) and the 6-button timeout (~1.5 ms) are from memory.
- **The licensed set** (No-Intro, 1,566 files after leaving out
  prototypes, betas, demos, pirate and unlicensed dumps; `genesis_headless
  --frames 300`): 1,555 reach their first VDP port write, most in the
  first frame; Ghouls'n Ghosts in frame 64 and Phantasy Star II in 76
  (they checksum the ROM first). Not reached: six Game no Kanzume dumps
  (Mega-CD software: they wait on `TST.W $100000`), two Arcade Legends
  plug-and-play dumps (end in an exception handler), Triple Play 96 and
  Triple Play Gold (their own ROM mapping), Phantasy Star Fukkokuban
  (double fault). With TMSS enforced (version 1), 14 more lock: launch
  carts, After Burner II, Super Daisenryaku, the Mega-CD BIOS dumps and
  Micro Machines; so the default console is version 0.
- **Found on the way**: an address error whose own frame faults (an odd
  vector 3) recursed in `m68k_address_error` until the stack overflowed;
  it is now a double fault and the 68000 halts, as the 68000 manual
  says. The SingleStepTests m68000 corpus has no such test (it would have
  crashed the runner), so its results cannot change; not re-run here
  (the corpus is not on this machine).
- **Builds**: Linux gcc, all systems, `-DWERROR=ON`. Not built here:
  clang, MSVC (CI builds both).
- **Not checked**: public test ROMs (none found that report without a
  picture; roadmap D1), timing of the Z80's window into the 68000's
  space, H counter, open bus values.

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
