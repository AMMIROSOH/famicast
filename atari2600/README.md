# Atari 2600

The Atari 2600 interpreter is under development. It accepts raw `.a26` and
`.bin` cartridges and `.7z` archives containing one cartridge image.

```
cmake -S . -B build -DSYSTEMS=atari2600
cmake --build build --config Release
build/atari2600 "path/to/game.7z"
build/atari2600_headless "path/to/game.7z" --frames 120
```

To recompile a cartridge, configure with `-DROMS="path/to/game.7z"` and
build as above. The build traces scripted play and produces a game executable
and its `_headless` counterpart. The headless program accepts `--diff` to
compare each instruction against the interpreter and `--coverage` to report
compiled CPU cycles. `AUTO_HINTS=OFF` skips the trace and compiles the reset
entry. `EMBED_ROM=OFF` requires a ROM path at runtime.

For a game-specific configuration, put `region = "ntsc"` or `"pal"` and
`mapper = "2k"`, `"4k"`, `"f8"`, `"f6"`, or `"f4"` in
`atari2600/games/<target>.toml`, where `<target>` is the normalized build
target name. The same settings apply to tracing and generated programs.

The build fetches the public-domain LZMA SDK. The cartridge size selects 2K,
4K, F8, F6 or F4 banking. The frontend's `atari2600.ini` provides `mapper`
(`auto`, `2k`, `4k`, `f8`, `f6`, `f4`) and `region` (`auto`, `ntsc`, `pal`)
settings. The headless program accepts `--mapper` and `--region` overrides.
Auto region recognizes `(Europe)` and `(PAL)` in the file name.

Arrow keys and X control the first joystick. Enter and Right Shift operate the
console reset and select switches. F5 resets the emulator, F2 saves a state,
and F4 loads it. Two gamepads are supported.

TIA rendering and sound are still being validated against independent
hardware references. Object positioning and mid-scanline effects are
approximations.
Some cartridge formats are not implemented. Paddles, SECAM and cartridge RAM
are not supported yet.
