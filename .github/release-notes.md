retrocast is a static recompiler for retro consoles: it translates a game's machine code into C and builds it into a native program, with a small runtime per system and an SDL3 window, sound and gamepads. Recompiled games are checked cycle by cycle against a built-in reference interpreter.

Systems: NES and Famicom Disk System (6502), Game Boy and Game Boy Color (SM83), Genesis / Mega Drive (68000 and Z80), Atari 2600 (6507), Master System / Mark III and Game Gear (Z80).

No games are included: bring your own legally dumped ROMs and disk images (disk games also need the FDS BIOS; Game Boy games need no BIOS).

## Downloads

Windows (x64, arm64), Linux (x64, arm64; glibc 2.35+), macOS (Apple Silicon). Each archive has:

- `retrocast`: plays a game of any system in the interpreter; the system is read from the file (`--system` picks one when the file does not say)
- `nes_headless`, `gb_headless`, `genesis_headless`, `atari2600_headless`, `sms_headless` (Master System and Game Gear): the same without a window, one per system, for scripts and checks
- `nesrecomp`, `gbrecomp`, `mdrecomp`, `atari2600recomp`, `smsrecomp`: the recompilers (ROM to C)
- `kit/`: recompiles your own ROMs into native programs with only CMake and a C compiler installed (SDL3 comes prebuilt)

`README.txt` in the archive explains how to play and how to recompile. macOS builds are unsigned: run `xattr -dr com.apple.quarantine retrocast-*` once after unpacking.

**Do not share recompiled programs: they contain the game.**

Supported cartridges and known limitations: [NES](https://github.com/Viper-Bit/retrocast/blob/main/systems/nes/README.md#mappers), [Game Boy](https://github.com/Viper-Bit/retrocast/blob/main/systems/gb/README.md#not-yet), [Genesis](https://github.com/Viper-Bit/retrocast/blob/main/systems/genesis/README.md#status), [Atari 2600](https://github.com/Viper-Bit/retrocast/blob/main/systems/atari2600/README.md#status), [Master System and Game Gear](https://github.com/Viper-Bit/retrocast/blob/main/systems/mastersystem/README.md#status).
