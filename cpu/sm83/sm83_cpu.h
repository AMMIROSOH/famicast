/* SM83 (the Game Boy CPU) core: state, bus interface, interpreter.
 * Timing is counted in M-cycles (4 clocks): every bus access is one, and so
 * is every internal cycle (Pan Docs "CPU instruction set"; checked cycle by
 * cycle against SingleStepTests sm83, gb/docs/verification-log.md). */
#ifndef SM83_CPU_H
#define SM83_CPU_H

#include <stdint.h>

/* F: the low nibble always reads 0 */
enum { FLAG_Z = 0x80, FLAG_N = 0x40, FLAG_H = 0x20, FLAG_C = 0x10 };

typedef struct GbCpu {
    uint16_t pc, sp;
    uint8_t a, f, b, c, d, e, h, l;
    uint8_t ime;      /* interrupts enabled */
    uint8_t ei;       /* EI ran: IME turns on after the next instruction */
    uint8_t halted;   /* HALT: waiting for an interrupt (the system wakes it) */
    uint8_t stopped;  /* STOP: waiting for a button (the system wakes it) */
    uint8_t jammed;   /* one of the 11 unused opcodes locked the CPU up */
    uint8_t halt_bug; /* the next opcode fetch does not step PC (gb.c gb_step) */
    uint64_t cycles;  /* M-cycles since power-on */
    void *bus;        /* owner's context for gb_read/gb_write/gb_idle */
} GbCpu;

/* Implemented by whatever bus is linked with the core (Game Boy bus, test
 * bus). Each call is exactly one M-cycle; c->cycles is already incremented. */
uint8_t gb_read(GbCpu *c, uint16_t addr);
void gb_write(GbCpu *c, uint16_t addr, uint8_t v);
void gb_idle(GbCpu *c);  /* an internal cycle: no memory access */
/* Interrupt entry, after pushing PC's high byte: the vector of the highest
 * pending enabled interrupt, its IF bit cleared; 0 if none is left. */
uint16_t gb_irq_ack(GbCpu *c);

/* One instruction (or one cycle of a halted, stopped or jammed CPU). */
void gb_cpu_step(GbCpu *c);
/* Interrupt entry (the system calls it when IME is on and an enabled
 * interrupt is pending): IME off, 5 M-cycles. */
void gb_cpu_interrupt(GbCpu *c);

#endif
