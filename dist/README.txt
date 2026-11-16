famicast - static recompiler for NES games
https://github.com/Viper-Bit/famicast

No games are included. Use your own legally dumped ROMs (.nes files).


WHAT IS IN HERE

  nes             Plays a ROM in the built-in 6502 interpreter.
                  Usage: nes path/to/game.nes
  nes_headless    The same without a window, for scripts and checks
                  (--frames N, --record/--replay, --diff, --coverage, ...).
  nesrecomp       The recompiler: turns a ROM into C source.
  kit/            Everything needed to build a recompiled game into a
                  native program (see below), with SDL3 prebuilt.

On macOS, remove the download quarantine once before the first run:
  xattr -dr com.apple.quarantine famicast-*


PLAYING

Keyboard: arrows, X = A, Z = B, Enter = Start, Right Shift = Select,
F5 = reset, F2 / F4 = save / load state, F10 = TV frame,
F11 or Alt+Enter = fullscreen, Esc = quit.
Gamepads work too (first = player 1, second = player 2).
Display settings are in nes.ini next to the program, created on first run.


RECOMPILING A GAME (the kit)

A recompiled game is C code compiled on your machine; it then runs the game
natively. You need CMake 3.20 or newer and a C compiler:
  Windows: Visual Studio or its Build Tools ("Desktop development with C++")
           - the prebuilt SDL3 is built with MSVC, so MinGW will not link it
  Linux:   gcc or clang (e.g. sudo apt install build-essential cmake)
  macOS:   Xcode command line tools (xcode-select --install) and CMake

From the folder this file is in:

  cmake -S kit -B build -DNES_GAMES="/path/to/My Game.nes"
  cmake --build build --config Release

The program is build/my_game (build/my_game.exe on Windows), plus
build/my_game_headless. Several games: separate the paths with ";".
A relative path is relative to the folder you run cmake from.

Code the recompiler cannot find on its own runs in the interpreter. Two
things recompile more of it:
  - the build plays each game for a while first (scripted, about 6 seconds)
    and recompiles everything that ran; -DNES_AUTO_HINTS=OFF skips this;
  - your own play: the game notes code it had to interpret in
    build/my_game.misses, and the next
    "cmake --build build --config Release" recompiles it.

DO NOT SHARE RECOMPILED PROGRAMS: they contain the game.

Supported mappers and more: see the project page above.


LICENSES

famicast: GPL-3.0-or-later (LICENSE); the source is on the project page.
SDL3 (linked into nes and the kit): zlib license (LICENSE-SDL3.txt).
