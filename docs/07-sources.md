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
| nesdev wiki: MMC5 (PRG/CHR modes, $5105 nametable map, ExRAM modes, scanline IRQ and in-frame flag, multiplier), Namco 163 (registers, 15-bit IRQ, $4800/$F800 RAM port) | `runtime/cart.c`, `runtime/ppu.c` hooks (M7e) | from memory; CHR set A/B use with 8x8 sprites as Mesen2 does it (last set written), from memory |
| nesdev wiki: VRC2 and VRC4 (register map, board wirings per submapper, VRC2a CHR shift), VRC IRQ (prescaler 341 - 3 per cycle, cycle mode, ack) | `runtime/cart.c` (M7d) | from memory; checked against the games in docs/06 |
| nesdev wiki: MMC2, MMC4 (latch trigger addresses, latch set after the fetch), INES Mapper 018 (Jaleco SS88006: nibble registers, 4/8/12/16-bit IRQ counter), Sunsoft FME-7 (commands 0-F, ROM/RAM at $6000, IRQ on wrap from $0000), INES Mapper 210 (Namco 175/340, battery = 175 without a submapper) | `runtime/cart.c` (M7f) | read (public domain per the wiki's disclaimer) |
| nesdev wiki: VRC1 (75: PRG selects at $8000/$A000/$C000, $9000 mirroring and CHR bit 4, CHR low bits at $E000/$F000, mirroring ignored on four-screen boards) | `runtime/cart.c` (M7i) | read (public domain per the wiki's disclaimer) |
| nesdev wiki: TxSROM / INES Mapper 118 (CHR bank bit 7 drives CIRAM A10: R0/R1 for the nametable pairs, R2-R5 with CHR A12 inversion; mirroring register unconnected) | `runtime/cart.c` (M7h) | from memory; checked by a unit ROM and the games in docs/06 |
| nesdev wiki: Bandai FCG board, INES Mapper 016 (submappers 4/5: FCG-1/2 at $6000 with a direct counter, LZ93D50 at $8000 with a latch), 153, 157, 159 ($800D EEPROM lines, $6000 bit 4 SDA, X24C01 MSB-first) | `runtime/cart.c` (M7g) | read (public domain per the wiki's disclaimer); the I2C protocol (start/stop, acknowledge, 24C02 device byte, page writes of 8 and 4 bytes) from the 24C02/X24C01 datasheets and the IRQ at the clock that finds 0 as Mesen2 does it, from memory |
| nesdev wiki: NES 2.0 (mapper bits 8-11, submapper), INES Mapper 071, NINA-003-006 (79), INES Mapper 087, INES Mapper 185 (submappers 4-7), Namco 108 (206) | `runtime/cart.c` (M7c) | from memory; mapper 185 without a submapper uses the usual emulator guess (CHR on if latch & $0F and latch != $13), from memory |
| Mesen2 APU reset behaviour (keep $4017 mode, clear IRQ inhibit; pulse/noise length counters zeroed and unhalted, triangle unaffected) | `apu_reset()` | from memory |
| nesdev wiki "DMA" (halt, dummy and alignment cycles, get/put, DMC during OAM DMA) and Mesen2's DMA loop | `run_dma` in `runtime/nes.c` | from memory |
| Test sources from the pinned nes-test-roms mirror: cpu_interrupts_v2/source/4-irq_and_dma.s, apu_reset/source/*.s, dmc_dma_during_read4/source/*.s | expected outputs and what each test measures | read |
| [nesdev forum, "Help with dmc_dma [SOLVED]"](https://nesdev.nes.science/f3/t7547.xhtml) | sprdma_and_dmc_dma_512 rows $0A/$0B = 526/527 | read |
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
