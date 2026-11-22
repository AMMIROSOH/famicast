# CPUs

`cpu/` holds one folder per CPU. A CPU folder has no machine in it: no
cartridge, picture or sound. The system supplies the bus functions the CPU
calls, so every system with the same CPU uses the same code.

| CPU | Systems | Tests | Folder |
|-----|---------|-------|--------|
| 6502 (the NES's 2A03) | NES and Famicom Disk System | SingleStepTests 2A03, nestest | [6502/](6502/) |
| SM83 | Game Boy and Game Boy Color | SingleStepTests sm83 | [sm83/](sm83/) |

## What a CPU folder holds
- The instruction templates, written once: the opcode table and C per
  opcode (`ops.c`, `c6502.c`; `sm83.c`). The interpreter's switch and the
  recompiled code are both made from them.
- The interpreter: `cpu6502.c`, `cpu6502.h`, `cpu6502_ops.h`; `sm83_cpu.c`,
  `sm83_cpu.h`, `sm83_ops.h`.
- The switch generator (`cpu6502_gen.c`, `sm83_gen.c`), which writes the
  interpreter's opcode switch into `build/gen/` at build time.
- The CPU test runner (`sm83_tests.c`). The 6502's runner is
  `nes/tests/cpu_tests.c`, because it also runs nestest and recompiled code.
- `CMakeLists.txt`: the library and generator targets.

## How a system uses a CPU
1. The root `CMakeLists.txt` maps systems to CPUs (`set(CPU_nes 6502)`)
   and adds each selected CPU's folder once.
2. The system's `CMakeLists.txt` links the CPU library (`cpu6502` or
   `sm83`) and, for recompiled programs, the template library.
3. The system implements the bus functions its CPU calls: `cpu_read` and
   `cpu_write` for the 6502; `gb_read`, `gb_write`, `gb_idle` and
   `gb_irq_ack` for the SM83. The CPU's header declares them.

`tools/ci_systems.py` has the same table, so a change under a CPU's folder
makes CI test the systems that use it.

## Before two CPUs share one program
Each CPU is its own library, but the names are not all separate yet. These
clash today and are fine only while one CPU is linked into a program:
- The `FLAG_*` enums in both state headers (`cpu6502.h`, `sm83_cpu.h`).
- Static helpers in the ops headers: `rd`, `wr`, `fetch_k`, `op_ADC`,
  `op_SBC`, `op_AND`, `op_INC`, `op_DEC`, `op_BIT`, `push`, `pull`, `nz`,
  `idle`, `zf`.
- The 6502's link symbols without a prefix: `Op`, `OPS`, `ops_init`,
  `is_branch`, `page_penalty`, `body`, `emit_cpu_switch`, `cpu_reset`,
  `cpu_step`, `cpu_read`, `cpu_write`, and its type name `Cpu`.

A new CPU prefixes all of its names (`m68k_*`, `z80_*`). The 6502 gets a
prefix when a program first needs it alongside another CPU.
