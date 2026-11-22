# Atari 2600

The Atari 2600 part of [retrocast](../README.md) is under development.
`atari2600` plays a raw `.a26` cartridge, `atari2600_headless` runs it
without a window, and `atari2600recomp` recompiles it.

## Status

- **Cartridges**: 2K, 4K, F8, F6 and F4 banking; NTSC and PAL.
- **Hardware**: TIA playfield, horizontal motion and audio are clocked
  independently. Some cartridge formats, cartridge RAM, paddles and SECAM
  are not supported yet.
- **Recompilation**: traced and statically reachable instructions become C;
  code not found at build time falls back to the interpreter. The headless
  executable can compare each instruction against the interpreter with
  `--diff`, and report compiled CPU cycles with `--coverage`.

```
cmake -S . -B build -DSYSTEMS=atari2600
cmake --build build --config Release
build/atari2600 path/to/game.a26
build/atari2600_headless path/to/game.a26 --frames 120
```

To recompile, configure with `-DROMS="path/to/game.a26"` and build as
above. The build traces scripted play and produces a game executable and its
`_headless` counterpart. `AUTO_HINTS=OFF` skips the trace and compiles the
reset entry. `EMBED_ROM=OFF` requires a ROM path at runtime.

For a game-specific configuration, put `region = "ntsc"` or `"pal"` and
`mapper = "2k"`, `"4k"`, `"f8"`, `"f6"`, or `"f4"` in
`atari2600/games/<target>.toml`, where `<target>` is the normalized build
target name. The same settings apply to tracing and generated programs.

## Controls and settings

Arrow keys and X control the first joystick. Enter and Right Shift operate
the console reset and select switches. F5 resets the emulator, F2 saves a
state, and F4 loads it. Two gamepads are supported.

The frontend's `atari2600.ini` provides `mapper` (`auto`, `2k`, `4k`, `f8`,
`f6`, `f4`) and `region` (`auto`, `ntsc`, `pal`) settings. The headless
program accepts `--mapper` and `--region` overrides. Auto region recognizes
`(Europe)` and `(PAL)` in the file name.

Both players accept `--record FILE` and `--replay FILE` for controller and
reset inputs. The headless runner defaults to the recording's length when
replaying; `--play` generates scripted inputs for a headless recording.

## Docs

| File | Content |
|------|---------|
| [roadmap.md](docs/roadmap.md) | Remaining hardware work |
| [verification-log.md](docs/verification-log.md) | Tests and game checks |
