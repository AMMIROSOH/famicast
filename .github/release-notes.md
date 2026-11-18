retrocast is a static recompiler for NES and Famicom Disk System games: it translates a game's 6502 code into C and builds it into a native program, with a small runtime for the PPU, APU, mappers and an SDL3 window, sound and gamepads. Recompiled games are checked cycle by cycle against a built-in reference interpreter.

No games are included: bring your own legally dumped ROMs and disk images (disk games also need the FDS BIOS).

## Downloads

Windows (x64, arm64), Linux (x64, arm64; glibc 2.35+), macOS (Apple Silicon). Each archive has:

- `nes`: plays a game in the interpreter
- `nes_headless`: the same without a window, for scripts and checks
- `nesrecomp`: the recompiler (ROM to C)
- `kit/`: recompiles your own ROMs into native programs with only CMake and a C compiler installed (SDL3 comes prebuilt)

`README.txt` in the archive explains how to play and how to recompile. macOS builds are unsigned: run `xattr -dr com.apple.quarantine retrocast-*` once after unpacking.

**Do not share recompiled programs: they contain the game.**

Supported mappers and known limitations: see the [README](https://github.com/Viper-Bit/retrocast#mappers).
