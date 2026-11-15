/* 2A03 CPU core: state, bus interface, interpreter entry points. */
#ifndef NES_CPU_H
#define NES_CPU_H

#include <stdint.h>

/* Status register bits, real 6502 layout (docs/03). B and U exist only in
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
    uint64_t cycles;  /* CPU cycles since power-on; +1 per bus access */
    void *bus;        /* owner's context for cpu_read/cpu_write */
} Cpu;

/* Implemented by whatever bus is linked with the core (NES bus, test bus).
 * Each call is exactly one CPU cycle; c->cycles is already incremented. */
uint8_t cpu_read(Cpu *c, uint16_t addr);
void cpu_write(Cpu *c, uint16_t addr, uint8_t v);

void cpu_reset(Cpu *c);  /* 7-cycle reset sequence; PC from $FFFC */
void cpu_step(Cpu *c);   /* one instruction, or one interrupt entry */

#endif
