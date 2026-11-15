# 07 · Sources and credits

Every external source we use goes here: what it is, what we used it for,
and its license if we take code or data from it. If code is copied or adapted,
the file header must also say where it came from.

Status: **read** = we used it directly; **listed** = found, not read in
detail yet.

## Hardware documentation
| Source | Used for | Status |
|--------|----------|--------|
| [nesdev wiki](https://www.nesdev.org/wiki/Nesdev_Wiki) (CPU, PPU, APU, mapper pages) | Main hardware reference for everything in 03/04 | read (in part) |
| [nesdev wiki · Emulator tests](https://www.nesdev.org/wiki/Emulator_tests) | Test ROM list; nestest automation mode (start at $C000, compare to log made with Nintendulator) | read |
| nesdev wiki: PPU registers, PPU scrolling (loopy v/t/x/w), PPU rendering, PPU sprite evaluation, PPU frame timing, Open bus behavior, CPU interrupts, CPU memory map, NTSC video | PPU, bus and interrupt design in `runtime/`; behaviour then checked by the test ROMs | from memory, not re-read this session |
| Bisqwit's NTSC palette model (as described on nesdev "NTSC video") | `runtime/palette.c` signal levels and method | from memory |
| Mesen2's delayed rendering-enable flag | idea for the odd-frame skip timing (confirmed by blargg 10-even_odd_timing) | from memory |
| mmc3_test_2 (readme read), MMC1_A12 from the same mirror | MMC3 IRQ and A12 checks (M7) | freely distributed test ROMs | read |
| nesdev wiki: MMC1, UxROM, CNROM, MMC3 (IRQ counter, A12 filter, "cycle 260"), AxROM, Color Dreams, GxROM, PPU sprite fetches | `runtime/cart.c`, sprite fetch timing | from memory |
| Mesen2 APU reset behaviour (keep $4017 mode, clear IRQ inhibit; pulse/noise length counters zeroed and unhalted, triangle unaffected) | `apu_reset()` | from memory |
| nesdev wiki "INES" (DiskDude! junk in bytes 7-15) | header check in `runtime/cart.c`, used by `nesrecomp` too | from memory |
| Inigo Quilez, "2D distance functions" (rounded box SDF) | `runtime/platform/tv_frame.c` shapes | from memory |
| George Marsaglia, "Xorshift RNGs" (J. Stat. Software, 2003), the 13/17/5 xorshift32 | `nes_headless --play` input generator | from memory |
| nesdev wiki: APU, APU Frame Counter, Length Counter, Envelope, Sweep, Triangle, Noise, DMC, Mixer (output filters), CPU interrupts (branch polling, hijacking), DMA | `runtime/apu.c`, interrupt details | from memory |

## Test suites (downloaded by `tests/fetch_test_roms.py`, not committed)
| Source | Used for | License | Status |
|--------|----------|---------|--------|
| nestest.nes + nestest.log (Kevin Horton), from [qmtpro.com](https://www.qmtpro.com/~nes/misc/) (original) and the [christopherpow/nes-test-roms](https://github.com/christopherpow/nes-test-roms) mirror | Decoder test (M1); CPU trace comparison (M2/M5) | freely distributed test ROM | read |
| blargg and other test ROMs via [christopherpow/nes-test-roms](https://github.com/christopherpow/nes-test-roms), pinned at commit `95d8f621`: ppu_vbl_nmi, vbl_nmi_timing, sprite_hit_tests_2005, sprite_overflow_tests, blargg_ppu_tests_2005, ppu_open_bus, oam_read, oam_stress, instr_test-v5 (readmes and the 2005 test sources read for result protocols) | PPU/system checks (M3) | freely distributed test ROMs; check per archive before redistributing | read |
| More test ROMs from the same mirror: apu_test, blargg_apu_2005.07.30 (readme with frame-counter timing tables and sources read), apu_reset (readme read), cpu_interrupts_v2 (readme read), cpu_dummy_writes, sprdma_and_dmc_dma, dmc_dma_during_read4 | APU, interrupts, DMA (M6) | freely distributed test ROMs | read |
| [SingleStepTests/65x02 `nes6502/v1`](https://github.com/SingleStepTests/65x02/tree/main/nes6502), pinned at commit `b9cfa8b2` | 10,000 tests per opcode with the bus access of every cycle; interpreter check (M2) | MIT | read |

## Prior art
| Source | What we took | License | Status |
|--------|--------------|---------|--------|
| [Andrew Kelley, "Statically Recompiling NES Games into Native Executables with LLVM and Go"](https://andrewkelley.me/post/jamulator.html), [jamulator repo](https://github.com/andrewrk/jamulator) | Why pure static recompilation fails → interpreter fallback | — (ideas only) | search summary |
| [Alyssa Rosenzweig, "Solving BIT Magic"](https://rosenzweig.io/blog/solving-bit-magic.html) | Overlapping-instruction decoding | — (ideas only) | search summary |
| [NESRecomp (mstan)](https://github.com/mstan/nesrecomp) and ports, e.g. [Dr. Mario](https://raw.githubusercontent.com/mstan/DrMarioNesRecomp/HEAD/README.md) | 6502 → C + runner library; per-game `game.toml` | check before reuse | search summary |
| [NESgen (Xenomega)](https://www.github.com/Xenomega/NESgen) | Another NES → C recompiler | check | listed |
| [SuperMarioBros-C](https://github.com/MitchellSternke/SuperMarioBros-C) | Example of a game as readable C | check | listed |
| [N64Recomp](https://github.com/N64Recomp/N64Recomp) | Emit-C recompiler design on another system | check | listed |
| [FOSDEM 2026 talk, "The joys and horrors of NES dynamic recompilation"](https://fosdem.org/schedule/event/ETLVUA-the_joys_and_horrors_of_nes_dynamic_recompilation) | Context on dynarec pitfalls | — | listed |
| [RustyNES](https://github.com/doublegate/RustyNES): [testing-strategy.md](https://github.com/doublegate/RustyNES/blob/main/docs/testing-strategy.md), [scheduler.md](https://github.com/doublegate/RustyNES/blob/main/docs/scheduler.md) | Test harness protocol (nestest, blargg $6000), timing model | GPL-3.0-or-later | summary read |
| radare2 6502 + 65816 tables (`libr/arch/p/6502/6502dis.inc.c`, `libr/arch/p/snes/optable.h`) | Independent opcode table; extracted to `tests/data/r2_6502.json` | LGPL-3.0 | read |
| Mesen2 | PPU/APU/mapper reference; may adapt code | GPL-3.0 | listed |
| FCEUX | Second reference | GPL (check version) | listed |

## Libraries
| Library | Used for | License |
|---------|----------|---------|
| [SDL3](https://libsdl.org), tag `release-3.4.20` via CMake FetchContent | Window, audio, keyboard, gamepad | zlib |
