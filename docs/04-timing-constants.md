# 04 · Timing constants (NTSC)

Source: nesdev wiki "Cycle reference chart" and "PPU frame timing"
([07-sources.md](07-sources.md)).

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
advanced by 3 dots per CPU cycle when the bus catches it up. Never hard-code
"NMI at CPU cycle N".
