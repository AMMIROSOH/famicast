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

## Test suites (downloaded by `tests/fetch_test_roms.py`, not committed)
| Source | Used for | License | Status |
|--------|----------|---------|--------|
| nestest.nes + nestest.log (Kevin Horton) | CPU trace comparison, M1/M2/M5 | freely distributed test ROM | listed |
| blargg test ROMs (instr_test-v5, cpu_interrupts_v2, ppu_vbl_nmi, sprite_hit_tests, apu_test) | CPU/PPU/APU checks | check per archive | listed |
| SingleStepTests 6502 JSON tests (2A03 set) | Per-opcode state + cycle tests | check repo license | listed |

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
| Mesen2 | PPU/APU/mapper reference; may adapt code | GPL-3.0 | listed |
| FCEUX | Second reference | GPL (check version) | listed |

## Libraries
| Library | Used for | License |
|---------|----------|---------|
| [SDL3](https://libsdl.org) | Window, audio, keyboard, gamepad | zlib |
