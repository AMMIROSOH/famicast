# CPS-1 verification log

What was checked, how, and the result. Newest first. A claim without a
reproducible check is not "verified".

Checks of shared parts: [docs/verification-log.md](../../../docs/verification-log.md).

## 2026-11-23 · P3 · the YM2151 against ymfm
- **How**: Final Fight's 173,836 YM2151 register writes (80 s of play,
  logged with their sample) played through our YM2151 and through ymfm
  (Aaron Giles, BSD-3, commit 81aec25; built outside the repository, not
  linked into retrocast), compared in 73 ms windows; then each part alone
  with register scripts.
- **Before**: the LFO's rate and depths, the noise, and summed carriers
  (a 14-bit clip, the YM2612's) differed, and a decay at its sustain
  level took one step too many (-0.75 dB on a held note).
- **After**: pitch within 1.2 cents on 195 notes (key code, fraction, DT2)
  and DT1 0-7 exact; attack within 3% and release exact at every rate;
  vibrato and tremolo depth and rate for each waveform; noise at every
  frequency (level and spectrum identical); the 8 algorithms with
  feedback 55-60 dB below the signal; the whole song at the same level
  (median 0.0 dB) and spectrum (correlation median 1.00, centroid ratio
  1.00). `cps1_tests` pins the sustain hold, the LFO counter and the
  carrier sum.
- **Genesis**: the envelope's new order (the state changes before the
  step, as in ymfm's YM2612) leaves `md_tests`' YM2612 hash unchanged.

## 2026-11-23 · P3 · sound
- **Genesis unchanged**: a YM2612 program (two channels, algorithms 4
  and 7, feedback, the LFO) hashes the same before and after moving the
  operator to `core/audio/fm.c` (`md_tests`); the Genesis tests pass.
- **Self-checks** (`cps1_tests`): KC $4A is 437-443 Hz and $5A 874-886 Hz
  (zero crossings over a second); timer A of 1000 raises its IRQ at the
  24th sample, timer B of 255 at the 16th, a flag reset clears it; a note
  is silent within 2,000 samples of key off; OKI nibbles 0, 8, 8, 0 give
  2, 0, -2, 0; status, stop, and phrases that end before they start or
  lie past the ROM; the 68000's command reaches the Z80 (the last of two
  writes), the bank, the YM2151's IRQ reaching RST $38, 938 samples a
  frame and a full buffer that stops filling.
- **Final Fight** (`--play`, Start on the title): the attract mode is
  silent (as the game sets it); from the start of play, music and effects
  at an RMS of 1,500-3,200 a second; the first stage drawn right
  (`--ppm`, by eye).
- **Not checked**: by ear against the arcade; the LFO and noise.

## 2026-11-23 · P1-P2 · zips, the machine and the picture
- **Zips** (`tests/test_zip.py`): stored and deflated entries Python
  writes read back with the same CRC and size; a cut zip and one with a
  flipped data byte exit with an error; all 32 zips of the set read as
  Python reads them.
- **Self-checks** (`cps1_tests`): load layouts (even/odd bytes, word
  swap, graphics interleave), extra zip entries ignored, a missing file
  named by CRC, a non-CPS-1 zip scoring 0; VBlank three times in three
  frames, inputs active low, the CPS-B ID and multiplier, unmapped reads;
  the palette's brightness rule, a tile of each scroll layer at its
  screen pixel, a flipped two-block sprite, a high-priority scroll2 pen
  over a sprite.
- **Final Fight** (`cps1_headless ffight.zip --frames 1200 --play --ppm`,
  checked by eye): the warning screen at frame 600, the title with
  "CREDIT =1" at 1,200, in the right colours. The CPS-B-04 palette
  control is at $2A (where the game writes $3F).
- **Detection**: `ffight.zip` is `cps1`; CPS-1 games not yet in the table
  and a CPS-2 zip are not.
- **Builds**: MinGW gcc with `-DWERROR=ON -DTESTS=ON`, every system; the
  Genesis tests pass on its 68000 copy.
- **Not checked**: sound, the window, other games.
