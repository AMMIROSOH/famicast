/* 2A03 CPU core: state, bus interface, interpreter entry points. */
#ifndef CPU6502_H
#define CPU6502_H

#include <stdint.h>

/* Two systems with this CPU in one program (the NES and the Atari 2600 in
 * retrocast) each get their own copy of the interpreter: CPU6502_PREFIX
 * (cpu6502_library() in CMakeLists.txt) renames its functions and the bus
 * functions it calls, so the calls stay direct. */
#ifdef CPU6502_PREFIX
#define CPU6502_CAT2(p, n) p##n
#define CPU6502_CAT(p, n) CPU6502_CAT2(p, n)
#define cpu_read CPU6502_CAT(CPU6502_PREFIX, cpu_read)
#define cpu_write CPU6502_CAT(CPU6502_PREFIX, cpu_write)
#define cpu_reset CPU6502_CAT(CPU6502_PREFIX, cpu_reset)
#define cpu_step CPU6502_CAT(CPU6502_PREFIX, cpu_step)
#endif

/* Status register bits, real 6502 layout (systems/nes/docs/6502-traps.md). B and U exist only in
 * copies pushed to the stack; the register keeps U set and B clear. */
enum {
    FLAG_C = 0x01, FLAG_Z = 0x02, FLAG_I = 0x04, FLAG_D = 0x08,
    FLAG_B = 0x10, FLAG_U = 0x20, FLAG_V = 0x40, FLAG_N = 0x80
};

typedef struct Cpu {
    uint16_t pc;
    uint8_t a, x, y, s, p;
    uint8_t jammed;   /* a KIL opcode stopped the CPU */
    uint8_t nmi;      /* NMI edge seen, taken before the next instruction */
    uint8_t irq;      /* IRQ line level, nonzero = asserted */
    uint8_t poll, poll_prev;  /* interrupt wanted, sampled after each cycle */
    uint8_t yield;    /* recompiled code must return to the dispatcher instead of
                         chaining to the next block (frame end, lockstep tests) */
    uint64_t cycles;  /* CPU cycles since power-on; +1 per bus access */
    void *bus;        /* owner's context for cpu_read/cpu_write */
    uint8_t open_bus; /* last value on the data bus */
    uint8_t decimal_enabled; /* NMOS 6502 decimal arithmetic; disabled for NES 2A03 */
    /* Optional fast path, set up by the bus (all NULL/0 = every access goes
     * through cpu_read/cpu_write, as the test bus wants). read_map/write_map:
     * per 2 KB page, memory that can be accessed directly with no side
     * effects. bus_pending/bus_budget: the bus's lazy clock (nes.c: master
     * clocks not yet run, and how many may pile up before the PPU must run);
     * bus_step is added per fast access. */
    const uint8_t *read_map[32];
    uint8_t *write_map[32];
    int32_t bus_pending, bus_budget, bus_step;
} Cpu;

/* Implemented by whatever bus is linked with the core (NES bus, test bus).
 * Each call is exactly one CPU cycle; c->cycles is already incremented. */
uint8_t cpu_read(Cpu *c, uint16_t addr);
void cpu_write(Cpu *c, uint16_t addr, uint8_t v);

void cpu_reset(Cpu *c);  /* 7-cycle reset sequence; PC from $FFFC */
void cpu_step(Cpu *c);   /* one instruction, or one interrupt entry */

#endif
