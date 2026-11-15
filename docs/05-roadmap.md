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
- [x] 52/52 test ROMs pass (`tests/test_ppu.py`, list in 06).
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
- `emit_c.py`: one C function per block, all bus access through the runtime,
  run-time penalty cycles, interrupt checkpoints (see 02).
- Dispatch with interpreter fallback; log every fallback address.
- Lockstep diff mode (`--diff`): recompiled vs interpreter, compare state +
  RAM hash per block, stop at first difference with a readable report.
- **Exit:** recompiled nestest matches the log; Battle City runs 10 minutes of
  recorded input with zero divergences in `--diff` and zero fallbacks after
  `game.toml` is filled in.

## M6 · APU + audio
- Pulse ×2, triangle, noise, DMC, frame counter + IRQ; SDL3 audio stream with
  resampling.
- **Exit:** blargg `apu_test` passes; Battle City sounds right by ear compared
  with a reference emulator.

## M7 · More mappers and games
- MMC1, UxROM, CNROM, then MMC3 (scanline IRQ). Bank-aware block keys.
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
