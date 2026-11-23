# Master System verification log

What was checked, how, and the result. Newest first. A claim without a
reproducible check is not "verified".

Checks of shared parts: [docs/verification-log.md](../../../docs/verification-log.md).

## 2026-11-23 · S4 · the Game Gear mode
- **Self-checks** (`sms_tests`): the mode from the header and the file
  name, two-byte palette writes to colours 3 and 31, ----BBBBGGGGRRRR
  colours, port $00 with and without Start (export and Japan), Start
  without an NMI, port $00 writes not reaching $3E, 60 Hz only.
- **Games** (900 frames of `--play`, `--ppm` checked by eye): Aladdin
  (beta), Columns, Defenders of Oasis (beta) and Micro Machines (beta, the
  Codemasters mapper) show their title or intro pictures in the right
  colours.
- **Boot test** (`TestGameGearBoot`, `--play` taps Start on a Game Gear
  during the title screens): Columns, Ecco - The Tides of Time, Defenders of
  Oasis and Micro Machines end on pictures of 4+ colours. Ax Battler was
  left out: its story text runs 70 s before the title, which Start does not
  skip (it reaches the title at 4,500 frames).
- **Recompiled** (`-DROMS`, 3,000 frames of `--play --diff --coverage`):
  Columns 99.1% and Micro Machines 94.5% of cycles recompiled, no
  difference.
- **Builds**: MinGW gcc with `-DWERROR=ON -DTESTS=ON`, every system;
  `test_detect` agrees between the C detectors and the CMake rules.
- **Not checked**: the window program by hand; stereo (not done).

## 2026-11-23 · S3 · the Z80 recompiler
- **Random instructions** (`smstest_random`, `tests/random_rom.c`): 6,391
  instructions from the base, CB, ED, DD and FD tables, memory writes
  through HL, BC, DE, IX, IY and (nn) pointed at RAM, conditional JR, DJNZ
  and JP over a few NOPs. All recompiled (100.00% of cycles); 600 frames,
  no difference. Mutation checks: without the jump end of a block after
  `@B` (conditional branches) the diff fails in frame 0; without `qprev`
  cleared after a DD/FD prefix it fails in frame 6.
- **Games** (built through `-DROMS` with the 6,000-frame trace, then 3,000
  frames of `--play --diff --coverage`): no difference in any of the ten.
  Recompiled share: Alex Kidd in Miracle World 89.3%, Captain Silver
  88.5%, Cosmic Spacehead 67.7%, Fantastic Dizzy 85.5%, Fantasy Zone
  92.6%, Hang On 85.7%, Micro Machines 82.8%, Phantasy Star 99.4%, R-Type
  99.9%, Wonder Boy in Monster Land 99.3% (the three Codemasters games on
  their own mapper).
- **Speed** (the same 3,000 frames, best of two): the recompiled programs
  take 0.9-1.0 times the interpreter's time (about 1 s each). Without
  drawing the picture, the interpreter needs 0.27 s of Phantasy Star's
  1.01 s: the CPU is a quarter of the work, and recompiling it does not
  make it cheaper here.
- **Builds**: MinGW gcc with `-DWERROR=ON`, every system with the frontend
  and two recompiled games (`NAME` and `NAME_headless`); the Python suite
  with `RETROCAST_STRICT=1`. MSVC with `-DWERROR=ON` for the Master System.
- **Not checked**: the window program's `NAME.misses` round trip by hand
  (the same functions as `--misses`, which the build uses); public Z80
  test ROMs against the recompiler.

## 2026-11-23 · S1-S2 · the machine, detection and window program
- **Self-checks** (`sms_tests`): the header and its region, the Sega
  mapper (power-on banks, the fixed first 1 KB, bank wrap, registers
  written through to RAM, cartridge RAM's two banks), the Codemasters
  mapper, pads and the TH lines, port decoding by A7/A6/A0, the V counter's
  jump, the VDP's read buffer, registers and CRAM, colour decoding, a
  4-bitplane tile plain, flipped and scrolled, a sprite from the split
  table, the hidden left column, the ninth sprite on a line, one frame
  interrupt a frame, the line interrupt's period, a frame's T-states (262
  and 313 lines of 228), 735-736 samples a frame, Pause as an NMI on the
  press only, a state saved and loaded. All pass.
- **Games** (`sms_headless --play`, every 5th game of the No-Intro set in
  `D:\roms\Sega - Master System - Mark III`, BIOS images left out, 137
  games; the most colours on screen at frames 300, 900, 1,500 and 3,000):
  124 reach a title screen or play, by the count or checked by eye (Great
  Golf, The Simpsons - Bart vs. The World). Not: 11 Korean unlicensed
  games (other mappers, or the VDP's older TMS9918 modes, not modelled), a
  prototype that never turns the display on (Sonic's Edusoft), and The
  Addams Family after the scripted button presses (its intro, attract mode
  and title play without input; with them the screen stays one colour,
  not yet investigated). Pictures looked at: Alex Kidd in Miracle World,
  Fantasy Zone, Hang On, R-Type, Wonder Boy in Monster Land, Phantasy Star
  (title, story, town), Cosmic Spacehead, Fantastic Dizzy, Micro Machines.
- **Sound**: Alex Kidd, 10 s: 735.9 samples a frame (44,100 Hz at 59.92
  frames a second), peak at a third of full scale, nothing clipped. Not
  compared with a recording of a console.
- **Detection** (`tests/test_detect.py`, `system_detect` and
  `cmake/detect_system.cmake` on the same files): headers with a Master
  System region, a headerless `.sms` (weak, the extension decides), the
  same file as `.bin` refused, a Game Gear header refused. All 137 sampled
  games are named `sms`.
- **Builds**: MinGW gcc with `-DWERROR=ON`: Genesis and Master System
  alone, and every system with the frontend (`retrocast` links the
  Genesis's Z80 and this system's prefixed copy). The whole Python suite
  passes with `RETROCAST_STRICT=1` (157 tests; the skips need game ROMs of
  other systems).
- **Not checked**: public test ROMs (ZEXALL on the console, VDP test
  ROMs); PAL timing and the picture against a console.
