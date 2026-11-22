# Master System verification log

What was checked, how, and the result. Newest first. A claim without a
reproducible check is not "verified".

Checks of shared parts: [docs/verification-log.md](../../../docs/verification-log.md).

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
