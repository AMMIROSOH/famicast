# 05 · Roadmap

Order rule: **build the reference before the thing it checks.** The
interpreter and test harness come before the recompiler, so the recompiler is
never judged against itself. Each milestone has an exit test; it is "done"
only when that test passes in CI.

## Target layout
```
nes/
  CMakeLists.txt
  recomp/            Python: ines.py, ops.py, decoder.py, c6502.py (C per opcode),
                     blocks.py, emit_c.py, cli.py
  tools/             gen_cpu_switch.py, import_r2_6502.py
  runtime/
    cpu.h cpu_ops.h cpu.c  bus.c  ppu.c  apu.c  input.c  dispatch.c
    mappers/  nrom.c mmc1.c uxrom.c cnrom.c ...
    platform/ sdl3_main.c
    tests/    cpu_tests.c
  games/battlecity.toml   per-game hints (extra entry points, jump tables)
  tests/
    fetch_test_roms.py    downloads public test ROMs into tests/roms/ (ignored)
    test_ops.py test_decoder.py ...
    cpu_runner.c          runs nestest / JSON tests against interp6502
  docs/
```

## M0 · Repo and build skeleton
- [x] git, GPL-3.0, corrected docs, `.gitignore` for ROMs.
- [x] CMake project: `nescpu` static lib, `nes` executable, SDL3 3.4.20 via
  FetchContent (static), `-DNES_FRONTEND=OFF` for headless builds.
- [x] Old prototype moved to `legacy/`.
- [ ] GitHub Actions (Windows + Linux build, Python + C tests) once the repo
  has a GitHub remote.
- **Exit:** `build/nes --frames 120` opens the window and exits 0 (done
  locally); CI green (pending remote).

## M1 · Opcode table and decoder (Python)
- [x] `recomp/ops.py`: all 256 opcodes, checked against radare2's tables
  (254/256 agree; the 2 differences are radare2 errors, see 06).
- [x] `recomp/ines.py`: iNES / NES 2.0 header, rewritten from the spec.
- [x] `recomp/decoder.py`: recursive descent, overlapping decodes allowed,
  records indirect jumps and out-of-ROM targets.
- [x] Test: with the dynamic targets fed back, every ROM instruction in
  `nestest.log` is decoded with the same bytes and mnemonic.
- [ ] `game.toml` loader (entry points, `no_return` dispatchers).
- [ ] Battle City: inspect its 8 `JMP (ind)` sites and write its `game.toml`.
- **Exit:** the above, plus a decode listing tool for debugging.

## M2 · Reference interpreter (C)
- [x] `runtime/cpu.c` + `cpu_ops.h`, opcode switch generated from
  `recomp/ops.py`; one bus access per cycle.
- [x] `runtime/tests/cpu_tests.c` on a flat 64 KB bus; driven by
  `tests/test_cpu.py`.
- [x] nestest: all 8,991 lines match, unofficial opcodes included.
- [x] SingleStepTests: all 244 non-KIL opcodes pass 10,000/10,000, bus cycles included.
- [x] Interrupt polling on the exact cycle (done in M3).
- **Exit:** met.

## M3 · PPU + NROM + video (interpreter only)
- [x] `runtime/cart.c` (NROM), `ppu.c` (per-dot rendering, loopy scrolling,
  per-dot sprite evaluation with the overflow bug, vblank/NMI races, open-bus
  decay), `nes.c` (CPU bus, OAM DMA, controllers), `palette.c` (NTSC model).
- [x] CPU interrupt polling on the second-to-last cycle (`cpu_ops.h`).
- [x] SDL3 frontend: ROM loading, keyboard, integer scaling, 60.1 Hz pacing.
- [x] `nes_headless` runner: blargg $6000 protocol, zero-page result codes,
  PPM screenshots, frame hash, scripted input (`--press`).
- [x] 52/52 test ROMs pass (`tests/test_roms.py`, list in 06).
- [x] Battle City: title screen and stage 1 render and play (interpreter).
- [ ] `test_ppu_read_buffer` (needs mapper 3, M7).
- **Exit:** met.

## M4 · Input
- [x] $4016/$4017 controller shift registers, keyboard (done in M3).
- [x] SDL3 gamepads: two players, hot-plug, D-pad or left stick.
- [x] Opposite directions (left+right, up+down) filtered, as on a real pad.
- [x] Input recordings (`runtime/input_log.c`, `.nesi`): `--record` /
  `--replay` in both `nes` and `nes_headless`; reset presses are recorded.
- [x] `tests/test_input.py`: record → replay gives the identical frame;
  a recording written by Python matches; reset flag replays (uses a local
  Battle City ROM found by SHA-256, `tests/games.py`; skips without it).
- [ ] Configurable key/button mapping: not needed yet; add when asked.
- **Exit:** met for keyboard and recordings; gamepad code builds and runs but
  was not tested with a physical pad on the dev machine (none connected).

## M5 · Recompiler v1 (NROM)
- [x] `recomp/blocks.py`, `recomp/emit_c.py`, CLI `python -m recomp ROM -o OUT.c --hints`.
- [x] Runtime dispatch (`nes_step`): block if recompiled, else interpreter;
  PRG-hash check; fallback statistics.
- [x] `nes_headless --diff` (lockstep vs interpreter: CPU, RAM, PRG RAM, PPU
  state per block, frame image per frame) and `--misses`.
- [x] Feedback loop: `python -m recomp.suggest`, `games/battle_city.toml`.
- [x] CMake: `nes_recompile()`; nestest always, own ROMs via `-DNES_GAMES=`.
- [x] Recompiled nestest: all 3,752 block-boundary states match nestest.log.
- [x] Battle City: 10 minutes of scripted play (`tests/play_script.py`),
  zero divergences, zero interpreted instructions.
- **Exit:** met.

## M5b · Speed
- [x] `tools/bench.py`: best of 3 runs (desktop timings vary ±20%).
- [x] Lazy PPU catch-up (`nes.c`): dots are counted and run in batches when
  the CPU touches the PPU, OAM DMA runs, or vblank (NMI) is due.
- [x] `ppu_run`: skips vblank lines in one step; sprite line buffer
  (per-pixel lookup instead of scanning 8 sprites).
- [x] Memory-map fast path in `rd`/`wr` (`cpu.h` read/write maps): RAM,
  PRG RAM and ROM accesses are inline loads; helps both modes.
- [x] Recompiled code with static PC: code bytes and PC values are constants
  (`fetch_k`, `emit_c.static_body`).
- Result (6,000 frames, best of 3): interpreter 27.7x → 31.0x real time,
  recompiled 18.5x → 30.5x. CPU side alone (PPU stubbed): recompiled
  1.8x faster than the interpreter (123x vs 69x real time). The PPU is now
  ~75% of the time in both modes, so overall they are at parity.
- [ ] Optional later: a faster PPU (specialized loop for visible-line dots, or a
  scanline renderer used when no CPU access falls inside the line), checked
  against the per-dot PPU with the test ROMs and frame hashes.

## M6 · APU + audio
- [x] `runtime/apu.c`: pulse ×2 (envelope, sweep), triangle, noise, DMC with
  sample DMA and IRQ, frame counter (blargg 2005 timing, jitter), mixer
  tables, 48 kHz box-filter resampling, NES output filters.
- [x] Lazy like the PPU: frame-IRQ and DMC-fetch events in the bus budget.
- [x] CPU interrupt details: NMI hijacks BRK/IRQ in their first four cycles,
  no polling at the end of an interrupt sequence, branch polling rules.
- [x] SDL3 audio stream with ±0.5% rate control; `--wav`, `--audio-hash`.
- [x] Tests: apu_test 8/8, blargg_apu_2005 11/11, apu_reset 4/6,
  cpu_interrupts_v2 4/5, cpu_dummy_writes 2/2; pitch test (`tests/test_apu.py`).
- [ ] Pending, with reasons in `tests/test_roms.py`: apu_reset reset-case
  (2), cpu_interrupts_v2 4-irq_and_dma and sprdma_and_dmc_dma (2): need
  interrupt polling and DMC fetches during OAM DMA; dmc_dma_during_read4
  (repeated halted read not modelled; prints CRCs only).
- [ ] Battle City by ear vs a reference emulator (user).
- **Exit:** met except the pending items above.

## M7 · More mappers and games
M7a (runtime):
- [x] Mappers 0, 1 (MMC1, incl. SUROM 512 KB and the ignored consecutive
  write), 2, 3, 4 (MMC3 rev B with A12-clocked IRQ), 7, 11, 66: 1,313 of the
  1,460 ROMs in the local collection.
- [x] PPU sprite fetches spread over dots 257-320 (removes the M3 shortcut);
  every PPU bus address reported to the cart (A12, with the M2 filter).
- [x] Bus: mapper writes catch up the PPU first (mid-frame bank switches);
  PRG fast-path pages follow the bank tables; MMC3 IRQ catch-up points.
- [x] Battery saves (`ROM.sav`) in the frontend.
- [x] Tests: mmc3_test_2 5/6 (6 = rev A, pending), test_ppu_read_buffer and
  cpu_dummy_reads (CNROM) pass; `tools/smoke_games.py` boots a sample of the
  local collection per mapper (52/53 ok, see 06).
M7b (recompiler):
- [ ] Bank-aware blocks: code compiled per (PRG bank, CPU window), looked up
  by the bank currently mapped; feedback loop extended to banks.
- **Exit:** at least one game per mapper runs in `--diff` with no divergence.

## M8 · Tooling and polish
- Coverage report (share of cycles in recompiled vs fallback code).
- Auto-suggest `game.toml` entries from fallback logs.
- Save states (interpreter and recompiled share one state format).
- Optional: frame-hash regression suite for several games.

## Stretch
- Direct block-to-block jumps to cut dispatch overhead.
- Analysis build: symbols per block for IDA/Ghidra, with a write-up.
- Hand-written x86-64 backend, checked against the C backend with `--diff`.
