# 6502 / 2A03 traps

Details that are easy to get wrong. Each item says how we test it.
Primary references: nesdev wiki (CPU, Status flags, Stack, NMI pages), see
[sources.md](../../../docs/sources.md).

## CPU
- **2A03 = 6502 without decimal mode.** The D flag can be set and is pushed
  with P, but ADC/SBC always work in binary. No 65C02 opcodes (no PHX/PLX/PHY/PLY,
  no STZ/BRA). `$0A/$2A/$4A/$6A` are ASL/ROL/LSR/ROR on A.
- **Flag bits**: C=0x01 Z=0x02 I=0x04 D=0x08 B=0x10 (only exists in pushed
  copies) bit5=1 when pushed, V=0x40 N=0x80. PHP and BRK push B=1; NMI and IRQ
  push B=0. PLP and RTI ignore bits 4 and 5.
- **ADC/SBC**: carry out is `sum >= 0x100` (not `>`). V = `(~(A^M) & (A^R)) & 0x80`.
  SBC is ADC with `M ^ 0xFF`.
- **Read-modify-write (INC/DEC/ASL/LSR/ROL/ROR on memory)**: the address and
  the value are separate; write the new value back to the original address.
  Real hardware writes the old value first and then the new one ("dummy
  write"); this matters for some mapper registers.
- **Addressing modes**:
  - `abs,X` / `abs,Y`: address = operand + X/Y (16-bit wrap). It is not a
    pointer.
  - `(zp),Y`: 2-byte instruction; pointer read from zp and zp+1 **wrapping
    inside page 0** (`$FF` reads hi from `$00`), then + Y.
  - `(zp,X)`: pointer at `(zp + X) & 0xFF` and `(zp + X + 1) & 0xFF`.
  - `zp,X` / `zp,Y`: wrap inside page 0.
  - `JMP ($xxFF)`: hi byte read from `$xx00` (page-wrap bug, real behavior).
- **Cycle penalties**: +1 for page cross on reads with `abs,X`, `abs,Y`,
  `(zp),Y` (depends on X/Y, so computed at run time); stores and RMW always pay
  it. Branch taken +1, taken to another page +2.
- **Stack**: page 1, `$0100 + S`, S wraps 8-bit. JSR pushes (return address
  − 1); RTS pulls and adds 1. RTI pulls P then PC, without the +1.
- **Interrupts**: NMI fires on PPU vblank start **only if PPUCTRL bit 7 is
  set**, and is not affected by the I flag. IRQ (APU frame counter, DMC,
  mappers) is masked by I. The CLI/SEI/PLP effect on IRQ is delayed by one
  instruction.
- **Interrupt polling exceptions**: branches poll before their 2nd cycle and
  before the page fix-up (a taken branch without page cross ignores its last
  cycle); BRK/IRQ/NMI sequences do not poll at their end, so the handler's
  first instruction always runs; an NMI in the first four cycles of BRK or an
  IRQ entry takes over the vector (BRK still pushes B=1).
- **$AB LXA / $8B XAA are unstable** (vary between chips). Test suites
  disagree on $AB; we follow the hardware-verified blargg test (see 06).
- **Illegal opcodes**: real games use a few (e.g. `NOP` variants, `LAX`,
  `SAX`). All 244 non-KIL opcodes are implemented and pass the
  SingleStepTests; the recompiler emits the same C for them.

Tests: nestest log, SingleStepTests per-opcode JSON (cover every flag and
addressing case above), blargg `instr_test-v5`, `cpu_interrupts_v2`.

## PPU / bus
- Reading $2002 clears the vblank flag and the $2005/$2006 write toggle.
  Reading it one dot before vblank starts suppresses the flag and the NMI;
  reading on the first two dots of vblank returns the flag but drops the NMI.
- NMI is edge-triggered: enabling it (PPUCTRL bit 7) while the vblank flag is
  already set fires an NMI right after the next instruction.
- $2003 sets OAMADDR; $2004 writes increment it; $4014 copies a whole page to
  OAM and stalls the CPU 513 or 514 cycles.
- $2005/$2006/$2007 are PPU registers (scroll, VRAM address, VRAM data), not
  APU. $2007 reads are buffered (one read delayed), except palette reads.
- Any read or write instruction can touch I/O: `STX $2006`, `BIT $2002`,
  `LDA $2007,X` all count. Every access goes through the bus; do not try to
  list I/O instructions ahead of time.

## APU
- Within one CPU cycle: the bus access sees the APU state of the previous
  cycle, then the APU clocks, then the CPU samples IRQ. The IRQ line lags one
  cycle, like $4015 reads.
- $4017 takes effect on even CPU cycles, one cycle later on odd ones; the
  frame IRQ flag is set on three consecutive cycles (29830-29832 after the
  write); a length reload on the clocking cycle is ignored if the counter
  was non-zero; a halt-flag write takes effect after that cycle's clock.
- At power and reset, $4017 acts as written 9-12 cycles before the first
  instruction (we use 10).

## iNES headers
- Old dumps carry junk such as "DiskDude!" in bytes 7-15. If bytes 12-15 are
  not zero and the header is not NES 2.0, ignore byte 7 (else mapper 2 can
  read as 66).

## Lessons from the first prototype
The first prototype failed on these points; see
[first-prototype.md](first-prototype.md) for the whole story.
- **Lifting a load to a no-op loses its flags.** A load that the driver
  performs must still set N/Z.
- **Two addressing modes in one tag**: `JMP (abs)` (3 bytes) and `(zp),Y`
  (2 bytes) must be separate modes.
- **"Runs N frames without halting" proves nothing.** A run is only "correct"
  if it matches an independent reference state by state.
