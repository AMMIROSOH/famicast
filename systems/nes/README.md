# NES and Famicom Disk System

The NES part of [retrocast](../../README.md): `retrocast` plays a game in the
interpreter, `nes_headless` runs one without a window, `nesrecomp`
recompiles one (through `-DROMS`, see the [root README](../../README.md#recompiling-a-game)).

## Status
- **CPU**: the interpreter matches nestest and the SingleStepTests (every
  opcode, unofficial ones included, cycle by cycle); recompiled code is
  the same C per instruction.
- **Picture and sound**: the PPU, APU and DMA pass the blargg-style test
  ROMs listed in [docs/verification-log.md](docs/verification-log.md) (a
  few timing tests open). Expansion sound: VRC6, VRC7, Sunsoft 5B, Namco
  163, MMC5 and FDS.
- **Games**: 74 mappers (99.7% of licensed releases load) and the Famicom
  Disk System; NTSC, PAL and Dendy timing.
- **Recompiled**: most games run above 99.5% of their CPU time as
  recompiled code (less for games that run code from RAM), identical to
  the interpreter in lockstep.
- **Speed**: about 33x real time interpreted and 35x recompiled on one
  desktop core (a test game, MSVC build). The picture and sound chips take
  most of the time, so recompiling gains little over the interpreter here.

## Mappers
The mapper is the cartridge's banking hardware; the iNES header names it.
What the numbers mean, from weakest to strongest:

- **Loads**: the mapper is supported. That is about 99.7% of licensed
  releases in a ~4,000-ROM survey (85% of the whole set, with unlicensed and
  pirate carts). A game that loads may still hit something not emulated
  (see below).
- **Boots**: for each mapper, one dump of every licensed title was run with
  scripted input and reached a title, menu or play screen.
- **Lockstep-tested**: one or more games per mapper (per board family for
  the newest) ran recompiled and interpreted side by side (`--diff`, 3,000
  frames or more) with identical CPU, RAM, PPU state and frames.
- **Recompiled share**: in those runs above 99.5% of CPU cycles ran as
  recompiled code for most games (up to 99.98%); the rest is interrupt entry, which always
  goes through the interpreter, code in RAM, and code the scripted play
  found that the build had not.

Each is listed per mapper in [docs/verification-log.md](docs/verification-log.md).

| Mapper | Board | Examples |
|---|---|---|
| 0 | NROM | Super Mario Bros., Donkey Kong, Excitebike |
| 1 | MMC1 (incl. SUROM 512 KB) | The Legend of Zelda, Metroid, Mega Man 2 |
| 2 | UxROM | Mega Man, Castlevania, Contra |
| 3 | CNROM | Gradius, Paperboy, Solomon's Key |
| 4 | MMC3 | Super Mario Bros. 3, Kirby's Adventure, Mega Man 3 |
| 5 | MMC5 (with its sound and split screen) | Castlevania III, Uncharted Waters, Metal Slader Glory |
| 7 | AxROM | Battletoads, Marble Madness, R.C. Pro-Am |
| 9 | MMC2 | Mike Tyson's Punch-Out!!, Punch-Out!! |
| 10 | MMC4 | Fire Emblem (Japan), Fire Emblem Gaiden, Famicom Wars |
| 11 | Color Dreams | Crystal Mines, Bible Adventures, Chiller |
| 13 | CPROM (16 KB CHR RAM) | Videomation |
| 16 | Bandai FCG-1/2, LZ93D50 (24C02 EEPROM saves) | Dragon Ball Z II, SD Gundam Gaiden 2, Rokudenashi Blues |
| 18 | Jaleco SS88006 (no speech chip) | Pizza Pop!, Ninja Jajamaru: Ginga Daisakusen, Saiyuuki World 2 |
| 19 | Namco 163 (with its wavetable sound) | Rolling Thunder (Japan), Megami Tensei II, King of Kings |
| 21 | Konami VRC4a/VRC4c | Wai Wai World 2, Ganbare Goemon Gaiden 2 |
| 22 | Konami VRC2a | TwinBee 3, Ganbare Pennant Race! |
| 23 | Konami VRC2b/VRC4e/VRC4f | Contra (Japan), Crisis Force, Parodius Da! |
| 24 | Konami VRC6a (with its sound) | Akumajou Densetsu |
| 25 | Konami VRC2c/VRC4b/VRC4d | Gradius II, Teenage Mutant Ninja Turtles (Japan), Bio Miracle Bokutte Upa |
| 26 | Konami VRC6b (with its sound) | Mouryou Senki Madara, Esper Dream 2 |
| 32 | Irem G-101 | Image Fight (Japan), Major League, Kaiketsu Yancha Maru 2 |
| 33 | Taito TC0190 | Akira, Don Doko Don, Insector X |
| 34 | BNROM, AVE NINA-001 | Deadly Towers, Mashou, Impossible Mission II |
| 37 | MMC3 multicart | Super Mario Bros. + Tetris + Nintendo World Cup |
| 41 | Caltron 6-in-1 | Caltron 6 in 1 |
| 47 | MMC3 multicart | Super Spike V'Ball + Nintendo World Cup |
| 48 | Taito TC0690 | Bubble Bobble 2 (Japan), The Flintstones (Japan), The Jetsons (Japan) |
| 64 | Tengen RAMBO-1 | Shinobi, Klax, Rolling Thunder |
| 65 | Irem H3001 | Spartan X 2, Daiku no Gen-san 2, Kaiketsu Yancha Maru 3 |
| 66 | GxROM | Super Mario Bros. / Duck Hunt, Dragon Power, Gumshoe |
| 67 | Sunsoft-3 | Fantasy Zone II (Japan), Mito Koumon: Sekai Manyuu Ki |
| 68 | Sunsoft-4 (no sub-cartridge licence timer) | After Burner, Maharaja, Nantettatte!! Baseball |
| 69 | Sunsoft FME-7 / 5B (with its sound) | Batman: Return of the Joker, Mr. Gimmick, Hebereke |
| 70 | Bandai 74161 | Family Trainer 5, Kamen Rider Club, Space Shadow |
| 71 | Camerica / Codemasters | Micro Machines, The Fantastic Adventures of Dizzy, Bee 52 |
| 72 | Jaleco JF-17 (no speech chip) | Pinball Quest (Japan), Moero!! Pro Tennis, Moero!! Juudou Warriors |
| 73 | Konami VRC3 | Salamander (Japan) |
| 75 | Konami VRC1 | Ganbare Goemon! Karakuri Douchuu, King Kong 2: Ikari no Megaton Punch, Tetsuwan Atom |
| 76 | Namco 3446 | Digital Devil Story: Megami Tensei |
| 77 | Irem (CHR ROM and RAM, four screens) | Napoleon Senki |
| 78 | Irem / Jaleco JF-16 | Holy Diver, Uchuusen Cosmo Carrier |
| 79 | AVE NINA-03/06 | Krazy Kreatures, Dudes with Attitude, Deathbots |
| 80 | Taito X1-005 | Mirai Shinwa Jarvas, Minelvaton Saga, Kyonshiizu 2 |
| 82 | Taito X1-017 | Kyuukyoku Harikiri Stadium III, SD Keiji: Blader |
| 85 | Konami VRC7 (with its FM sound) | Lagrange Point, Tiny Toon Adventures 2 (Japan) |
| 86 | Jaleco JF-13 (no speech chip) | Moero!! Pro Yakyuu |
| 87 | Jaleco J87 | Argus, City Connection, The Goonies (Japan) |
| 88 | Namco 3433 | Dragon Spirit: Aratanaru Densetsu, Quinty, Namcot Mahjong III |
| 89 | Sunsoft-2 (one-screen board) | Tenka no Goikenban: Mito Koumon |
| 92 | Jaleco JF-19 (no speech chip) | Moero!! Pro Soccer, Moero!! Pro Yakyuu '88 |
| 93 | Sunsoft-2 (CHR RAM board) | Shanghai, Fantasy Zone (Japan) |
| 94 | UN1ROM | Senjou no Ookami |
| 95 | Namco 3425 | Dragon Buster (Japan) |
| 97 | Irem TAM-S1 | Kaiketsu Yancha Maru |
| 113 | HES | HES 6-Pak, Mindblower Pak, Total Funpak |
| 118 | TxSROM (MMC3 with nametables from CHR bank bit 7) | Ys III (Japan), Pro Sport Hockey, NES Play Action Football |
| 119 | TQROM (MMC3 with CHR ROM and RAM) | High Speed, Pin Bot |
| 140 | Jaleco JF-11/JF-14 | Bio Senshi Dan, Mississippi Satsujin Jiken, Youkai Club |
| 152 | Bandai 74161 with one-screen mirroring | Arkanoid II, Saint Seiya: Ougon Densetsu, Gegege no Kitarou 2 |
| 153 | Bandai LZ93D50 with 8 KB battery RAM | Famicom Jump II |
| 154 | Namco 3453 | Devil Man |
| 155 | MMC1A | The Money Game, Tatakae!! Rahmen Man |
| 157 | Bandai Datach (no barcode reader: card scans cannot be made) | Battle Rush, Dragon Ball Z: Gekitou Tenkaichi Budoukai, Ultraman Club |
| 158 | Tengen RAMBO-1 with nametables from CHR bank bit 7 | Alien Syndrome |
| 159 | Bandai LZ93D50 with X24C01 EEPROM saves | Dragon Ball Z: Kyoushuu! Saiya Jin, Magical Taruruuto-kun, SD Gundam Gaiden |
| 180 | Nichibutsu UNROM ($C000 switched) | Crazy Climber (Japan) |
| 184 | Sunsoft-1 | Atlantis no Nazo, Madoola no Tsubasa, Kanshakudama Nage Kantarou |
| 185 | CNROM with CHR copy protection | B-Wings, Bird Week, Mighty Bomb Jack (Japan) |
| 188 | Bandai Karaoke Studio (microphone never pressed) | Karaoke Studio |
| 206 | Namco 108 | Karnov, Gauntlet (unlicensed), Dragon Buster II |
| 207 | Taito X1-005 with nametables from CHR bank bit 7 | Fudou Myouou Den |
| 210 | Namco 175/340 | Famista '91-'94, Splatterhouse: Wanpaku Graffiti, Wagyan Land 3 |
| 228 | Active Enterprises | Action 52, Cheetahmen II |
| 232 | Camerica Quattro | Quattro Adventure, Quattro Sports, Quattro Arcade |

Not yet: the last licensed boards (Oeka Kids 96, the Nintendo World
Championships carts 105 and 555, Konami VRC5 547), the Datach barcode
reader and the long tail of unlicensed and pirate boards.
Other mappers are refused at load ("mapper N not supported yet").

## Famicom Disk System
`.fds` disk images (with or without the 16-byte header) play like
cartridges. They need the FDS BIOS (8 KB, not included): put it next to the
program as `disksys.rom`, or set `fds_bios` in the .ini.
- **Disk sides**: when a game asks for another side, it is swapped in by
  itself (`fds_auto_disk = 0` turns that off); F6 puts in the next side by
  hand.
- **Saves**: what a game writes to its disk is kept in `ROM.sav`.
- **Recompiling**: see [Recompiling](#recompiling). In tests, about
  99% of CPU time ran recompiled.
- **Headless**: `nes_headless` takes `--bios FILE` and `--disk F:S` (swap to
  side S at frame F).
- **Not yet**: two unlicensed disks (one stays black, one jams).

## Recompiling
`.nes` and `.fds` files go in `ROMS` (root README). Disk games need the
BIOS: `-DROMS="Disk Game.fds" -DFDS_BIOS=path/to/disksys.rom`; the program
then carries the BIOS as well. `FDS_BIOS` alone also builds `build/fds`,
which plays any `.fds` image with only the BIOS recompiled;
`systems/nes/games/fds.toml` holds BIOS entry points (Rev 1 BIOS; another revision
only recompiles less).

Code the static decoder cannot find (jump tables, RTS used as a jump) runs in
the interpreter. Three ways to recompile it too:
- **Automatically** (`AUTO_HINTS`, on by default): the build plays each
  game for `AUTO_HINTS_FRAMES` frames (`nes_headless --play 1`) and passes
  every address it ran to the recompiler. Merged hints are written to
  `build/gen/<name>.toml`; copy them to `systems/nes/games/` to keep them without
  tracing.
- **By playing**: a recompiled game notes code it had to interpret in
  `build/<name>.misses` (setting `record_misses` in its `.ini`); the next
  `cmake --build build --config Release` sees the file changed and
  recompiles the game with those addresses.
- **By hand**, from a recording (reaches what the script cannot):
  ```
  build/retrocast game.nes --record run.rcin
  build/nes_headless game.nes --replay run.rcin --misses --out misses.txt
  build/nesrecomp game.nes --hints systems/nes/games/game.toml --misses misses.txt --save-hints systems/nes/games/game.toml
  cmake --build build --config Release
  ```

`build/game_one_headless --play 1 --diff --misses` runs the interpreter in
lockstep and reports the first difference, then what was still interpreted.
Hints live in `systems/nes/games/<name>.toml` and record the PRG hash of the dump
they were made for. The options were called `NES_GAMES`, `NES_EMBED_ROM`,
`NES_AUTO_HINTS(_FRAMES)`, `NES_FRONTEND` and `NES_SDL3_PREFIX` before; a
build folder configured with them keeps its values under the new names.

## Keys and settings
F6: next disk side (FDS). NES settings in `nes.ini` (or the game's .ini),
beside the shared ones:
```
overscan = 1        # 1 = hide the top/bottom 8 lines, as a TV did
overscan_sides = 0  # 1 = hide the left/right 8 columns too
sprite_limit = 1    # 0 = draw every sprite (less flicker; a few games glitch)
region = auto       # auto (from the ROM header), ntsc, pal, dendy
fds_bios = disksys.rom  # FDS BIOS: next to the program or the disk image, or a full path
fds_auto_disk = 1   # 1 = swap disk sides when a game asks
```
- **Aspect**: `aspect = tv` shows 8:7 pixels on NTSC, 1.39:1 on PAL.
- **Region**: NTSC games forced to `pal` run slower but usually work;
  PAL-only games on `ntsc` often freeze or jam, as on a real NTSC console.

## Tests
nestest is downloaded on first run (hash-checked). Recompiled games of the
build get lockstep, save-state and frame-hash tests
(`tests/nes/data/frame_hashes.json`); after an intended output change:
`NES_UPDATE_HASHES=1 python -m unittest tests.nes.test_regression`.

## Docs
| File | Content |
|------|---------|
| [architecture.md](docs/architecture.md) | Components, CPU and bus, banked and disk code, DMA |
| [roadmap.md](docs/roadmap.md) | Milestones M0-M11 and the speed passes |
| [verification-log.md](docs/verification-log.md) | What was checked, how, and the result (per mapper too) |
| [6502-traps.md](docs/6502-traps.md) | 6502/2A03 details that are easy to get wrong |
| [timing-constants.md](docs/timing-constants.md) | NTSC, PAL and Dendy clock and frame timing |
| [first-prototype.md](docs/first-prototype.md) | The retired first prototype and why it failed |
