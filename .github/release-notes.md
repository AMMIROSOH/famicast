retrocast is a static recompiler for retro consoles: it translates a game's machine code into C and builds it into a native program, with a small runtime per system and an SDL3 window, sound and gamepads. Recompiled games are checked cycle by cycle against a built-in reference interpreter.

Systems: NES and Famicom Disk System (6502), Game Boy and Game Boy Color (SM83), Genesis / Mega Drive (68000 and Z80), Atari 2600 (6507, in development).

No games are included: bring your own legally dumped ROMs and disk images (disk games also need the FDS BIOS; Game Boy games need no BIOS).

## Downloads

Windows (x64, arm64), Linux (x64, arm64; glibc 2.35+), macOS (Apple Silicon). Each archive has, per system:

- `nes`, `gb`, `genesis`, `atari2600`: play a game in the interpreter
- `nes_headless`, `gb_headless`, `genesis_headless`, `atari2600_headless`: the same without a window, for scripts and checks
- `nesrecomp`, `gbrecomp`: the recompilers (ROM to C)
- `kit/`: recompiles your own ROMs into native programs with only CMake and a C compiler installed (SDL3 comes prebuilt)

`README.txt` in the archive explains how to play and how to recompile. macOS builds are unsigned: run `xattr -dr com.apple.quarantine retrocast-*` once after unpacking.

**Do not share recompiled programs: they contain the game.**

Supported cartridges and known limitations: [NES](https://github.com/Viper-Bit/retrocast/blob/main/nes/README.md#mappers), [Game Boy](https://github.com/Viper-Bit/retrocast/blob/main/gb/README.md#not-yet).
