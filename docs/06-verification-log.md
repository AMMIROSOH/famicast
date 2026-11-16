# 06 · Verification log

What was checked, how, and the result. Newest first. A claim without a
reproducible check is not "verified".

## 2026-11-17 · M7j: Irem G-101 (32)
- **Unit ROM** (`tests/test_mappers.py`): PRG mode 1 puts the $8000
  register at $C000 and the second-to-last bank at $8000; a CHR bank at
  $1400; submapper 1 ignores $9000 and is one-screen page 1 (not the
  header's mirroring). Mutation caught: $9000 honoured on submapper 1.
- **Boot smoke**: the 7 licensed titles reach a title, menu or play screen,
  including Ai Sensei no Oshiete (the one game using PRG mode 1) and Major
  League (submapper 1 in its header).
- **Recompiled with `NES_AUTO_HINTS`, `--diff` 3,000 frames**: all identical.

  | Game | Recompiled cycles |
  |---|---|
  | Image Fight (Japan) | 99.98% |
  | Major League (Japan) | 99.98% |
  | Ai Sensei no Oshiete: Watashi no Hoshi (Japan) | 99.98% |

## 2026-11-17 · M7i: Konami VRC1 (75)
- **Unit ROM** (`tests/test_mappers.py`): PRG at $A000; CHR banks 17 and 2
  from the low bits at $E000/$F000 and $9000 bit 1 (the $0000 bank's bit 4
  only). Mutation caught: the two high bits swapped (the first version set
  both, which could not tell them apart).
- **Boot smoke**: the 7 licensed titles reach a title, menu or play screen.
  The Switch collection's Ninja JaJaMaru (an English re-release) has a
  NES 2.0 header with 256 KB PRG and battery RAM, beyond the chip's 4 PRG
  bits: it ran garbage from the wrong bank until a fifth bit was kept
  (no effect on 128 KB games, where it vanishes in the bank modulo).
- **Recompiled with `NES_AUTO_HINTS`, `--diff` 3,000 frames**: all identical.

  | Game | Recompiled cycles |
  |---|---|
  | Ganbare Goemon! Karakuri Douchuu (Japan, VC) | 99.98% |
  | King Kong 2: Ikari no Megaton Punch (Japan) | 99.98% |
  | Ninja JaJaMaru: The Ninja Skill Book (Switch) | 99.98% |

## 2026-11-17 · M7h: TxSROM (118)
- **Unit ROM** (`tests/test_mappers.py`): R0 = R1 = $80 makes all four
  nametables CIRAM page 1 ($2C00 shows a $2000 write; the mirroring
  register is ignored); with CHR A12 inversion R2-R5 = 0, 1, 0, 1 is
  vertical. Neither fits the header's mirroring. Mutation caught: R0/R1
  used in inversion mode too.
- **Boot smoke**: the 7 licensed titles reach a title, menu or play screen.
  Armadillo's "Battle Box not connected" is its save peripheral on the
  expansion port (not emulated); the game itself plays.
- **Recompiled with `NES_AUTO_HINTS`, `--diff` 3,000 frames**: all identical.

  | Game | Recompiled cycles |
  |---|---|
  | Ys III (Japan) | 99.97% |
  | Pro Sport Hockey (USA) | 99.95% |
  | Armadillo (Japan) | 99.94% |

## 2026-11-16 · M7g: Bandai FCG (16, 153, 157, 159)
- **Unit ROMs** (`tests/test_mappers.py`): PRG register and IRQ on time
  (counter 1000: 125 turns of a RAM loop) for the FCG-1/2 (registers at
  $6000, counter loaded directly) and the LZ93D50 ($8000, latch copied by
  the enable write); a byte written to a 24C02 (device $A0, address,
  data) and to an X24C01 (address + R/W byte), then read back by bit-banged
  I2C. Mutations caught: the LZ93D50 not copying its latch, EEPROM read
  bits sent LSB first. The first test byte, $5A, reads the same reversed;
  replaced by $4B.
- **Famicom Jump II** (153) jammed at frame 24 on zeroed battery RAM, as
  the wiki warns; it boots with RAM filled with $FF at power-on.
- **EEPROM probe** (temporary): Dragon Ball Z II, Battle Rush, SD Gundam
  Gaiden 2 and DBZ: Kyoushuu! Saiya Jin write their EEPROMs in 6,000
  frames of scripted play; the save menus show empty slots on a blank chip.
- **Boot smoke**: one dump of each of the 26 licensed titles (14 on 16, 1
  on 153, 7 Datach, 4 on 159) reaches a title, menu or play screen.
- **Recompiled with `NES_AUTO_HINTS`, `--diff` 3,000 frames**: all
  identical. Dragon Ball 3 switches PRG with stores to $6008 (the recompiler
  now treats $6000-$7FFF stores as possible bank switches for mapper 16).

  | Mapper | Game | Recompiled cycles |
  |---|---|---|
  | 16 (LZ93D50, 24C02) | Dragon Ball Z II (Japan, Rev 1) | 99.95% |
  | 16 (FCG-1/2) | Dragon Ball 3 (Japan, Rev 1) | 99.86% |
  | 153 | Famicom Jump II (Japan) | 99.89% |
  | 157 | Battle Rush (Japan, Datach) | 99.93% |
  | 159 | Magical Taruruuto-kun 2 (Japan) | 99.95% |

## 2026-11-16 · M7f: MMC2 (9), MMC4 (10), SS88006 (18), FME-7 (69), Namco 175/340 (210)
- **Unit ROMs** (`tests/test_mappers.py`): MMC2/MMC4 PRG and CHR latches
  (reading $0FD8 selects the $FD bank; $0FE9 switches the MMC4 only, as
  MMC2's left table triggers on $0FE8 exactly); SS88006 PRG from two
  nibbles and its IRQ in 12-bit mode on time (125 turns of a RAM loop;
  16 bits would be ~7,800); FME-7 PRG, ROM and then RAM at $6000, IRQ on
  time (125 turns); Namco 340 PRG and vertical mirroring from $E000.
  Mutations caught: MMC2 left latch on any row, 12-bit mode counting 16 bits.
- **Missed NMIs** (fixed separately, `tests/test_bus.py`): Plasma Ball,
  Top Striker and Chibi Maruko-chan hung on a black screen after one NMI.
  They enable NMI inside vblank and wait in a RAM loop, so one lazy batch
  ran from that vblank to the next and the NMI line went low and high
  inside it. Not mapper-specific; batches now also end at vblank end while
  the line is high.
- **Boot smoke**: one dump of each of the 46 licensed titles (2 MMC2, 3
  MMC4, 18 SS88006, 11 FME-7, 12 Namco 175/340) reaches a title, menu or
  play screen.
- **Recompiled with `NES_AUTO_HINTS`, `--diff`**: all identical.

  | Mapper | Game | Frames | Recompiled cycles |
  |---|---|---|---|
  | 9 | Punch-Out!! (Europe, VC) | 18,000 | 99.97% |
  | 10 | Fire Emblem (Japan) | 18,000 | 99.81% |
  | 18 | Saiyuuki World 2 (Japan) | 3,000 | 99.98% |
  | 18 | Plasma Ball (Japan) | 3,000 | 99.94% |
  | 69 | Batman (Japan) | 3,000 | 99.96% |
  | 210 | Splatterhouse: Wanpaku Graffiti (Japan) | 3,000 | 99.98% |
  | 210 | Top Striker (Japan, Namco 340) | 3,000 | 99.98% |

## 2026-11-16 · M7e: MMC5 (5) and Namco 163 (19)
- **Unit ROMs** (`tests/test_mappers.py`): MMC5 PRG mode 3, PRG RAM in a
  ROM window (written at $8005, read back through $6000 with the same
  bank), multiplier, fill-mode and ExRAM nametables, scanline IRQ at line
  100 arriving on time with no slow access (1,713 turns of a RAM loop,
  ~1,712 expected); Namco 163 PRG, a nametable from CHR ROM, the RAM port's
  auto-increment, cycle IRQ on time (125 turns). Mutations caught: no
  per-line catch-up for the MMC5 IRQ, the N163 IRQ planned 500 cycles late,
  RAM windows read as ROM. Two test-ROM mistakes found on the way: the
  scanline IRQ was already pending from the previous frame (now
  acknowledged before CLI), and the first expected loop count forgot the
  7 extra cycles per wrap of the low byte.
- **Probes** (temporary stderr reports, removed): all 21 MMC5 games write
  $5114-$5116 with bit 7 clear at some point, so PRG RAM windows were
  implemented rather than left as ROM; none ran code from one in the
  recompiled runs below. Three Namco 163 games write CHR values >= $E0
  (Megami Tensei II, Namco Classic I and II); all three set both $E800
  bits, which select CHR ROM for them, so reading CHR ROM is exact.
- **PRG RAM chips**: a battery save of Bandit Kings came out 16 KB, as its
  NES 2.0 header says (8 KB RAM + 8 KB NVRAM): two chips, picked by bit 2
  of the bank. The first version took the bank modulo the size, so banks 0
  and 4 shared memory; now bit 2 selects the chip (`mmc5_ram_off`), with a
  unit ROM that fails on the old mapping. The 21 boot frames are unchanged.
- **Boot smoke**: 21 MMC5 titles (one dump each) and 21 Namco 163 titles
  reach a title, menu, map or play screen; IRQ status bars (Castlevania
  III, Rolling Thunder, Youkai Douchuuki) split where they should; Metal
  Slader Glory's ExRAM pictures and the Koei strategy maps are correct.
  Uchuu Keibitai SDF runs without its split screen (not emulated).
- **Recompiled with `NES_AUTO_HINTS`, `--diff` 3,000 frames**: all identical.

  | Mapper | Game | Regions | Recompiled cycles |
  |---|---|---|---|
  | 5 | Castlevania III (Europe, VC) | 18 | 99.97% |
  | 5 | Metal Slader Glory (Japan, VC) | 5 | 99.96% |
  | 5 | Uncharted Waters (USA) | 10 | 99.98% |
  | 5 | Just Breed (Japan) | 13 | 99.95% |
  | 19 | Rolling Thunder (Japan) | 9 | 99.91% |
  | 19 | Megami Tensei II (Japan) | 10 | 99.98% |
  | 19 | Star Wars (Japan) | 8 | 99.98% |
  | 19 | King of Kings (Japan) | 6 | 99.98% |

## 2026-11-16 · M7d: Konami VRC2/VRC4 (21, 22, 23, 25)
- **Unit ROMs** (`tests/test_mappers.py`): VRC4e PRG swap (with submapper
  and with an old header, both wirings decoded), VRC2a CHR shift, VRC4b IRQ
  count over one frame (scanline mode, 20 lines: 13; cycle mode, 256
  cycles: 116), and IRQ arrival with no slow bus access in between (a RAM
  loop counts 284 iterations of 8 cycles = 2,272 of the expected 2,274).
  Mutations caught: the IRQ planned 1,000 cycles late, no VRC2a shift, a
  prescaler period of 300. (A prescaler step of 1 instead of 3 was not: the
  negative prescaler pays the missing clocks back at the next catch-up.)
  The first IRQ test read 3 instead of 13: the test ROM had left the APU
  frame IRQ on and never acknowledged it; it now writes $40 to $4017.
- **Boot smoke**: 15 of 15 games (every wiring: 21 sub 1/2, 22, 23 sub 2/3,
  25 sub 1/2/3) reach a title, menu or play screen; IRQ status bars (Tiny
  Toon, Bio Miracle, Gradius II, TMNT 2) split where they should.
- **Recompiled with `NES_AUTO_HINTS`, `--diff` 3,000 frames**: all identical.

  | Mapper | Game | Board | Regions | Recompiled cycles |
  |---|---|---|---|---|
  | 21 | Wai Wai World 2 | VRC4a | 17 | 99.96% |
  | 22 | TwinBee 3 | VRC2a | 10 | 99.98% |
  | 23 | Crisis Force | VRC4e | 11 | 99.91% |
  | 23 | Contra (Japan) | VRC2b | 9 | 99.98% |
  | 25 | Gradius II | VRC4b | 8 | 99.93% |
  | 25 | TMNT 2 (Japan) | VRC4d | 17 | 99.91% |

  Speed (headless, 6,000 frames of scripted play): Gradius II 20x, Contra
  18x real time; Super Mario Bros 19x measured the same way.

## 2026-11-16 · M7c: mappers 71, 79, 87, 185, 206
- **Header survey** of a full ~4,000-ROM set (first 16 bytes of each dump):
  all iNES; 18 NES 2.0 dumps on mappers 256-519 loaded as mapper 0-7
  because byte 8 was ignored (515 ran as CNROM). Fixed, test in
  `tests/test_ines.py`. The table of unsupported mappers by licensed count
  chose these five (206, 87, 185 among the most common; 79, 71 the most
  common unlicensed).
- **Unit ROMs** (`tests/test_mappers.py`): each switches a bank and stores
  the marker it reads. Mutation check: dropping mapper 87's swapped CHR bits
  or decoding mapper 79 at $4000 (A8 clear) fails the tests.
- **Boot smoke** (interpreter, 1,500 frames with Start presses): 15 of 15
  (three per mapper) reach a title, menu or play screen; the mapper 185
  games pass their CHR-protection check; four-screen Gauntlet (206) correct.
- **Recompiled, `--diff` 3,000 frames of scripted play**, one game per mapper:

  | Mapper | Game | Recompiled cycles (static) | With `NES_AUTO_HINTS` | `--diff` |
  |---|---|---|---|---|
  | 71 | Micro Machines (USA) | 0.3% | 99.97% | identical |
  | 79 | Krazy Kreatures (NINA-06) | 98.7% | 99.98% | identical |
  | 87 | City Connection (Japan) | 99.98% | (not needed) | identical |
  | 185 | B-Wings (Japan) | 49.5% | 99.98% | identical |
  | 206 | Karnov (USA) | 33.1% | 99.98% | identical |

  Mapper 79's register is at $4100, below ROM: the recompiler ends a block
  after any store at or above `prg_bank_reg()` (here $4100), so a bank
  switch there is seen like one at $8000.
- **Cross-platform frames**: Battle City frames 905-2400 from MSVC, MinGW
  GCC 15 and Linux GCC 13 (WSL) builds are byte-identical, interpreted and
  recompiled (a report of a missing shot on one Linux machine is therefore
  not in the emulation).

## 2026-11-16 · Open items pass
- **Play-to-recompile**: Kirby with no hints file: 7.2% of cycles recompiled
  before; after one 1,500-frame session in the real frontend (replaying the
  scripted input) and a rebuild, 99.94%.
- **Profiling**: a small sampler (suspends the process every ~1 ms, maps RIP
  to symbols with `nm`; scratch tool, not committed). Kirby, before:
  PPU ~58%, APU 16.5%, recompiled blocks + dispatch ~21% vs the
  interpreter's ~12% for the same CPU work. Code size, not dispatch: with
  the bus helpers inlined, thousands of blocks outgrew the caches (-Os and
  LTO did not help; out-of-line helpers did).
- **Speed** (best of 3, 6,000 frames of `--play 1`):

| Step | Kirby recompiled | Kirby interpreter | Battle City recompiled |
|---|---|---|---|
| before | 19.9x | 22.3x | 25.5x |
| bus helpers out of line in generated code | 23.4x | | 27.7x |
| block chaining (musttail) | 23.5x | | 27.8x |
| CHR/nametable reads inline, PPU visible-dot loop | 27.6x | 25.6x | 32.8x |
| PPU pixel step specialized (local line buffer) | 30.6x | 28.0x | 37.1x |
| APU mixer output cached | **32.6x** | **30.0x** | **38.0x** |

  Every step kept all frame and audio hashes and the test ROMs unchanged.
- **4-irq_and_dma**: not the DMA length. `apu_cycles_to_event` returned 2
  instead of 1 at frame counts 29830-29832, so when the bus synced on the
  exact cycle the IRQ was raised (as at the end of an OAM DMA), the next
  opcode fetch missed it. Found by logging DMA and IRQ cycles per test case
  and checking the four tail cases against the expected table.
- **DMA rework** (nesdev "DMA", Mesen2's structure): dmc_dma_during_read4
  dma_2007_read prints CRC 5E3DF9C4 (one of the two results its source
  documents), dma_4016_read, dma_2007_write, read_write_2007 pass (new
  `--text` check). sprdma_and_dmc_dma still fails: the only expected values
  found are rows $0A/$0B = 526/527 of the 512 variant (nesdev forum thread
  7547); ours are 526/528. Kirby's frame hash changed (DMC fetches now 3-4
  cycles); its `--diff` stays identical.
- **apu_reset 4017_written / len_ctrs_enabled**: a harness bug. After a
  reset, the test's reset code runs ~2 frames before it leaves $6000 = $81,
  and `nes_headless` pressed reset again. 6/6 pass now.
- **Straddling instructions**: compiled with the operand read at run time;
  Kirby's ROM misses are 0, interpreted steps 25,085 (RAM code only).

## 2026-11-15 · M8b: coverage, save states, frame hashes, frontend settings
- **Coverage** (`--play 1 --coverage`, 6,000 frames, share of CPU cycles):

| Game | Recompiled | Interpreted ROM | RAM code | Interrupt entry |
|---|---|---|---|---|
| Battle City, Metroid, Mega Man, Chiller, Arkista's Ring | 99.977% | 0.000% | 0.000% | 0.023% |
| Kirby's Adventure | 99.912% | 0.000% | 0.045% | 0.043% |
| Solstice | 99.967% | 0.000% | 0.010% | 0.023% |
| SMB / Duck Hunt | 99.972% | 0.000% | 0.005% | 0.023% |

  Interrupt entries (7 cycles each) always run in the interpreter, which is
  the floor here.
- **Save states** (`tests/test_states.py`): 250 frames + save, then load +
  150 frames gives the same frame and CPU cycle count as 400 frames in one
  go (vbl_nmi_timing/2, CHR RAM). The test's strength was checked by
  breaking the CHR RAM restore: a first version (instr_test-v5, CHR ROM; then
  saving before the font upload) still passed, so the ROM and save point
  were changed until the broken build failed. Battle City: a state saved by
  the interpreter (`--interp`) resumes in the recompiled build in lockstep
  (`--diff`, 600 frames) and ends on the uninterrupted run's frame. A state
  of another game is refused.
- **Frame hashes** (`tests/test_regression.py`): 8 games, 3,000 frames each,
  frame + cycles + audio recorded in `tests/data/frame_hashes.json`; 18 s.
- **Frontend**: game programs built as GUI programs on Windows (PE
  subsystem checked: 2 for the games and `nes`, 3 for `_headless`). Ran with
  TV frame + integer scale + fullscreen, and with bad `.ini` lines (reported,
  ignored). The TV frame was checked as an image (rendered over a Battle City
  frame); a seam at mid-height from a hard light switch was smoothed.

## 2026-11-15 · M8a: recompiler in C, embedded ROM, auto hints
- **Port check**: before deleting the Python recompiler, both versions
  generated code for nestest and the 8 games of the M7b table with the same
  hints (Python's region order made deterministic first). Output identical
  byte for byte apart from the header and the new embedded-ROM trailer; the
  interpreter switch (`nesrecomp --cpu-switch`) identical too. Kirby:
  0.15 s (C) vs 0.23 s (Python).
- **Tests**: those that imported `recomp/` now drive `nesrecomp` and read the
  generated C back (`tests/nesrecomp.py`); same assertions, 26/26 pass.
  A helper bug showed on the way: writing the IRQ vector with
  `data[-2:0] = ...` inserted bytes instead of replacing them.
- **Scripted input** moved from `tests/play_script.py` to
  `nes_headless --play SEED` (same pattern, xorshift32 instead of Python's
  `random`), so sessions differ from M7b. With the old hints, Metroid,
  Mega Man and SMB/Duck Hunt reached code not yet recompiled (38,990 /
  17,717 / 2,659 interpreted steps): what auto hints are for.
- **Trace**: `--misses` now counts in interpreter-only runs too, so one run
  of `nes_headless` lists every instruction executed.
- **Auto hints** (`-DNES_AUTO_HINTS=ON`, 6,000 frames, seed 1), then
  `--play 1 --diff --misses` on each build, ROM taken from the executable:

| Game | `--diff` | Interpreted steps | What remains |
|---|---|---|---|
| Battle City | identical | 0 | |
| Metroid (E) | identical | 0 | |
| Mega Man (E) | identical | 0 | |
| Arkista's Ring (U) | identical | 0 | |
| Kirby's Adventure (U) | identical | 25,103 | RAM code, 18 window straddles |
| Solstice (E) | identical | 5,996 | NMI handler in RAM |
| Chiller (U) | identical | 0 | |
| Super Mario Bros / Duck Hunt (U) | identical | 1,785 | RAM code |

  Kirby from no hints at all: one trace pass gives 25,103 interpreted steps,
  as good as the hand-run M7b loop (25,118). Building the 9 programs with
  tracing: 31 s (parallel). The merged hints were copied to `games/`, so the
  default build gives the same table.

## 2026-11-15 · M7b: bank-aware recompiler
Scripted play (`tests/play_script.py`, 6,000 frames, seed 1), feedback loop
(`--misses` → `recomp.suggest` → rebuild) until stable, then `--diff`:

| Mapper | Game (local dump) | Regions | Instructions | `--diff` | Interpreted steps |
|---|---|---|---|---|---|
| 0 | Battle City | 1 | 4,924 | identical | 0 |
| 1 | Metroid (E) | 3 | 8,239 | identical | 0 |
| 2 | Mega Man (E) | 4 | 8,556 | identical | 0 |
| 3 | Arkista's Ring (U) | 1 | 7,101 | identical | 0 |
| 4 | Kirby's Adventure (U) | 18 | 13,129 | identical | 25,118 (RAM code, 18 straddling) |
| 7 | Solstice (E) | 4 | 10,141 | identical | 5,996 = its NMI handler, in RAM |
| 11 | Chiller (U) | 1 | 3,614 | identical | 0 |
| 66 | Super Mario Bros / Duck Hunt (U) | 2 | 8,192 | identical | 1,785 (RAM code) |

Findings on the way:
- **Window-straddling instructions**: with 8 KB regions everywhere, Battle
  City's instruction at $DFFF (operand in the next window) went to the
  interpreter and 599,288 steps with it. NROM/CNROM now use one region;
  16/32 KB mappers use units of 2/4 windows (they switch together with
  consecutive banks). Only MMC3 keeps 8 KB units.
- **Loops entered from RAM**: Solstice's sprite-0 wait loop is entered from
  its NMI handler in RAM; every instruction of the loop is the target or
  fall-through of another, so `suggest` found no start. Fixed: unreached
  misses add their lowest address as a start (unit test added).
- **Header junk**: "Jackal (U) [T-Port]" has "DiskDude!" in bytes 7-15, which
  made mapper 2 look like 66. Byte 7 is now ignored when bytes 12-15 are not
  zero (nesdev "INES"); 1,356 of the 1,460 local ROMs use supported mappers.
- Build: CMake target names drop "(...)"/"[...]" tags; with Git Bash, pass
  Windows paths (`pwd -W`) in `NES_GAMES`, POSIX paths fail in Ninja.
- Speed (Kirby, best of 3): interpreter 22.1x, recompiled 19.2x real time.

## 2026-11-15 · M7a: mappers
- mmc3_test_2: 1-clocking, 2-details, 3-A12_clocking, 4-scanline_timing,
  5-MMC3 pass; 6-MMC3_alt (rev A behaviour) pending, it contradicts rev B.
  Fixes on the way:
  - after a $2007 access the PPU bus shows the incremented v (A12 can rise
    there): 3-A12_clocking;
  - the MMC3 IRQ was set inside a long PPU batch (line 238) and seen at
    vblank: catch-up points now at dots 0 and 255 of every rendering line
    while the IRQ is enabled, every cycle when the next clock can fire;
    with background at $1000 the counted rise is at dot 5 of the pre-render
    line (after vblank), which a fixed 255-330 window missed. Tried moving
    the sprite pattern address one dot earlier: fixed line 239, broke line 0;
    reverted (the real cause was the missed catch-up).
- test_ppu_read_buffer (CNROM) passes; cpu_dummy_reads (CNROM) prints
  "Passed" on screen (no $6000 signature; checked by screenshot).
  MMC1_A12 is an interactive visual test, not automated.
- Game smoke test (`tools/smoke_games.py`, local collection, good dumps,
  1,500 frames with Start presses): USA sample 32/32 and Europe sample 20/21
  boot and show content, checked on contact sheets (MMC3 IRQ splits in Power
  Blade, Kirby's Adventure, Mega Man 3 correct). The one blank is "3-in-1
  (E)", a multicart that needs a mapper-4 variant with an outer bank.
- Battery save: a seeded `.sav` is loaded and written back unchanged.
- Battle City: tests and 10-minute `--diff` unchanged.

## 2026-11-15 · M6: APU, audio, interrupt details
First run of the APU: 9/40 of the new ROMs passed. Final: all fetched ROM
tests pass except 7 listed as pending in `tests/test_roms.py`.

| Suite | Result |
|---|---|
| apu_test 1-8 | 8/8 |
| blargg_apu_2005.07.30 01-11 (result at $F0) | 11/11 |
| apu_reset | 4/6 (reset case of 4017_written, len_ctrs_enabled pending) |
| cpu_interrupts_v2 1-5 | 4/5 (4-irq_and_dma pending) |
| cpu_dummy_writes oam, ppumem | 2/2 |
| sprdma_and_dmc_dma, _512 | pending |
| pitch (tests/test_apu.py): pulse and triangle 440.4 Hz | within 2 Hz |

What it took, in order:
- **Result protocol**: blargg_apu_2005 keeps its result at $F0 (screen
  "$01"); dmc_dma_during_read4 prints CRCs without a pass/fail signal
  (dropped for now).
- **APU order within a CPU cycle**: the access sees the APU as of the
  previous cycle, then the APU clocks this cycle, then the CPU samples IRQ.
  Found by tracing 07.irq_flag_timing: a $4015 read on the event cycle saw
  the flag because the generic catch-up had already run that cycle. This one
  rule took the suite from 13 to 19 passing.
- **IRQ line lag**: the CPU sees the APU IRQ one cycle late, like $4015.
  Fixed 08.irq_timing.
- **Frame-counter jitter**: $4017 applies at once on even CPU cycles, one
  cycle later on odd ones (`FC_JITTER_PARITY = 0`; the other parity fails
  4-jitter).
- **Power/reset**: the effective $4017 write is 10 cycles before the first
  instruction. Measured with apu_reset 4017_timing, which prints the delay
  (8 − d for a delay d; started the counter at 2). Reset keeps the mode,
  clears IRQ inhibit, zeroes and unhalts pulse/noise length counters and
  leaves the triangle alone (as Mesen2). The remaining reset cases fail on a
  read where our result matches that model; no test source to check.
- **Branch interrupt polling**: polled before the 2nd cycle and before the
  page fix-up; a taken branch without page cross ignores the last cycle
  (5-branch_delays_irq).
- **NMI hijack**: an NMI in the first four cycles of BRK or IRQ entry takes
  over the vector; no polling at the end of the sequence (2-nmi_and_brk,
  3-nmi_and_irq).
- **OAM DMA length**: 514/515 cycles fixes 4-irq_and_dma but breaks
  sprite_overflow 3.Timing; kept nesdev's 513/514 and marked 4-irq_and_dma
  pending (needs polling during DMA).
- Mistake on the way: a timing sweep with an unanchored `sed` rewrote the
  $4017 jitter code too and made 12 tests fail; caught by the suite, fixed.
- Sound confirmed by ear by the user ("sound is perfect").
- Battle City: frame hash changed (EBB97DBA → 3A8AD2AE for 6,000 frames)
  because of the CPU interrupt fixes; 10-minute `--diff` identical, audio
  hash identical in both modes (FE2A5DF2, 28.75 M samples), title silent and
  stage-start music from Start onward. Speed: ~26x real time (APU stepped
  per cycle in both modes).

## 2026-11-15 · M5b: speed
Benchmark: `python tools/bench.py ROM EXE...` (Battle City, 6,000 frames of
`tests/play_script.py` seed 1, best of 3; single runs vary by ±20%).

| Step | Interpreter | Recompiled |
|---|---|---|
| M5 baseline | 27.7x | 18.5x |
| lazy PPU catch-up | ~28x | ~20x |
| + `ppu_run` vblank skip, sprite line buffer | 29.1x | 20.3x |
| + memory-map fast path in rd/wr | 31.0x | 29.7x |
| + static PC / constant code bytes | 31.0x | 30.5x |

(times in multiples of real NES speed)

Attribution (6,000 frames, interpreter): 3.39 s total; with no per-dot PPU
work 1.02 s. Pixel output ~0.8 s, background fetches ~0.55 s, sprite
evaluation ~0.27 s, per-dot bookkeeping ~0.75 s. With the PPU stubbed out,
CPU side only: interpreter 1.44 s, recompiled 0.81 s (1.8x).

Correctness after every step: frame hash of the 6,000-frame session
unchanged (EBB97DBA), 10-minute session unchanged (9A346143) and `--diff`
identical for 36,000 frames, all 52 test ROMs, nestest (interpreted and
recompiled), SingleStepTests.

Bug found on the way: the first lazy catch-up left the dot after a $2000
write pending, so enabling NMI during vblank fired late (ppu_vbl_nmi 04 and
07 failed). Fix: after any PPU register access, the rest of that CPU cycle
is run at once.

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
Run: `python -m unittest tests.test_roms` (ROMs from the nes-test-roms mirror,
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
