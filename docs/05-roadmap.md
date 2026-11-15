# 05 · Roadmap

Order rule: **build the reference before the thing it checks.** The
interpreter and test harness come before the recompiler, so the recompiler is
never judged against itself. Each milestone has an exit test; it is "done"
only when that test passes in CI.

## Target layout
```
nes/
  CMakeLists.txt
  recomp/            Python: ines.py, ops.py, decoder.py, blocks.py, emit_c.py, cli.py
  runtime/
    cpu.h  interp6502.c  bus.c  ppu.c  apu.c  input.c  dispatch.c
    mappers/  nrom.c mmc1.c uxrom.c cnrom.c ...
    platform/ sdl3_main.c
  games/battlecity.toml   per-game hints (extra entry points, jump tables)
  tests/
    fetch_test_roms.py    downloads public test ROMs into tests/roms/ (ignored)
    test_ops.py test_decoder.py ...
    cpu_runner.c          runs nestest / JSON tests against interp6502
  docs/
```

## M0 · Repo and build skeleton
- git, GPL-3.0, corrected docs, `.gitignore` for ROMs. *(done 2026-11-15)*
- CMake project: `runtime` static lib, `nes` executable, SDL3 via FetchContent.
- GitHub Actions: build on Windows + Linux, run Python and C tests.
- Move the old prototype to `legacy/` (or delete it) so it is not confused with
  the new code.
- **Exit:** `cmake --build` produces an SDL3 window that opens and closes;
  CI is green.

## M1 · Opcode table and decoder (Python)
- Re-check `ops.py` against an independent table for all 256 opcodes (official
  151 + illegal ones marked). Fix modes: separate `IND` (JMP) from `IZY`
  (`(zp),Y`) and `IZX` (`(zp,X)`).
- Decoder: recursive descent from vectors + `game.toml`, overlapping decodes
  allowed, data/code map output for debugging.
- **Exit:** unit tests for table and decoder; decoding nestest.nes from $C000
  reproduces the instruction addresses in `nestest.log`.

## M2 · Reference interpreter (C)
- `interp6502.c` over `bus.c`, flat 64 KB test bus for unit tests.
- **Exit:** `nestest.log` matches line for line (official opcodes first,
  then illegal ones); SingleStepTests 2A03 JSON set passes for every
  official opcode, cycle counts included.

## M3 · PPU + NROM + video (interpreter only)
- Mapper interface + NROM. PPU: registers, VRAM/palette, background and
  sprite rendering per dot or per scanline, vblank/NMI, sprite-0 hit, OAM DMA.
- SDL3 window, 256×240 texture, integer scaling, 60.1 Hz pacing.
- **Exit:** blargg `ppu_vbl_nmi` and `sprite_hit_tests` pass for the parts we
  claim; Battle City title screen renders in `--interp` mode.

## M4 · Input
- $4016/$4017 controller shift registers; SDL3 keyboard mapping + gamepad
  (hot-plug, two players).
- **Exit:** Battle City playable in `--interp` mode with keyboard and a pad.
  Record/replay of input to a file (needed for the regression tests below).

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
