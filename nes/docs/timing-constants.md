# Timing constants (NTSC; PAL and Dendy below)

Source: nesdev wiki "Cycle reference chart" and "PPU frame timing"
([sources.md](../../docs/sources.md)).

- Master clock 21.477272 MHz.
- CPU = master / 12 = **1.789773 MHz**.
- PPU = master / 4 → **exactly 3 PPU dots per CPU cycle**.
- Scanline = **341 dots**; frame = **262 scanlines** = 89,342 dots
  ≈ 29,780.67 CPU cycles ≈ 60.0988 Hz.
- On odd frames with rendering enabled, the pre-render line is one dot shorter
  (89,341 dots). This is why a frame is not a whole number of CPU cycles.
- Vblank flag set (and NMI, if PPUCTRL bit 7 = 1) at **scanline 241, dot 1**;
  cleared at the pre-render line (261), dot 1.
- APU frame counter runs on CPU cycles (4-step ≈ 29,830 cycles, 5-step
  ≈ 37,282); see nesdev "APU Frame Counter".
- OAM DMA ($4014): 513 CPU cycles, +1 if started on an odd CPU cycle.

Implementation rule: keep one master counter in CPU cycles; the PPU is
advanced by the dots of the elapsed master clocks when the bus catches it up
(NTSC: 3 per CPU cycle). Never hard-code "NMI at CPU cycle N".

## PAL and Dendy

The region comes from the NES 2.0 header (byte 12: 0 NTSC, 1 PAL,
2 multi-region, run as NTSC, 3 Dendy) or iNES byte 9 bit 0. Sources:
nesdev "Cycle reference chart", "APU Frame Counter", "APU Noise", "APU DMC".

| | NTSC | PAL | Dendy |
|---|---|---|---|
| Master clock | 21.477272 MHz | 26.601712 MHz | 26.601712 MHz |
| CPU | / 12 = 1.789773 MHz | / 16 = 1.662607 MHz | / 15 = 1.773448 MHz |
| PPU dot | / 4 (3 per CPU cycle) | / 5 (3.2 per CPU cycle) | / 5 (3) |
| Lines | 262 (pre-render 261) | 312 (311) | 312 (311) |
| Vblank flag / NMI | line 241 | line 241 (70 vblank lines) | line 291 (51 post-render lines) |
| Odd-frame dot skip | yes | no | no |
| Frame | 29,780.5 cycles, 60.0988 Hz | 33,247.5 cycles, 50.0070 Hz | 35,464 cycles, 50.0070 Hz |
| APU tables | NTSC | PAL noise, DMC, frame counter | NTSC |
| Emphasis bits 5, 6 | red, green | green, red | green, red |

The bus clock (`nes/runtime/nes.c`) counts master clocks: 12/16/15 a CPU
cycle, 4/5/5 a dot. PAL frame counter steps, in CPU cycles after a $4017
write as this runtime counts them: 8315, 16629, 24941, IRQ 33254-33256,
mode 1's last step 41567 (the wiki's APU cycles x 2 + 3, as the NTSC
7459 ... 29830 ... 37283).
