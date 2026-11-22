/* The 68000: state, bus interface, interpreter entry points.
 *
 * Timing is in clocks: a bus access takes 4, internal work the idle
 * clocks the microcode spends, in the order the CPU does them (checked
 * access by access against SingleStepTests m68000, generated from MAME's
 * microcoded 68000: cpu/README.md, genesis/docs/verification-log.md).
 *
 * The prefetch queue is the chip's: ir holds the opcode being decoded next,
 * irc the word after it, and pc is the address the next prefetch reads
 * (MAME's m_au, what the corpus calls pc): two words past the opcode. */
#ifndef M68K_CPU_H
#define M68K_CPU_H

#include <setjmp.h>
#include <stdint.h>

/* SR bits */
enum {
    M68K_C = 0x01, M68K_V = 0x02, M68K_Z = 0x04, M68K_N = 0x08, M68K_X = 0x10,
    M68K_S = 0x2000, M68K_T = 0x8000
};

typedef struct M68k {
    uint32_t d[8], a[8];  /* a[7] is the active stack pointer */
    uint32_t usp, ssp;    /* the inactive one is kept here (the active one in a[7]) */
    uint32_t pc;          /* next prefetch address (m_au) */
    uint16_t sr;
    uint16_t ir, irc;     /* prefetch queue */
    uint16_t op;          /* the instruction being executed (the frames' IR) */
    uint8_t stopped;      /* STOP: waiting for an interrupt */
    uint8_t halted;       /* double bus fault */
    uint8_t group0;       /* an address error's frame is being built: another one halts */
    uint8_t ipl;          /* interrupt level the system presents (0-7) */
    uint8_t ipl_seen;     /* level 7 is taken on its edge */
    uint8_t trace;        /* T was set when the instruction began */
    int8_t ae_adj;        /* an address error pushes pc + ae_adj (-2 unless the instruction says) */
    uint64_t cycles;      /* clocks since power-on */
    void *bus;            /* the owner's context for the functions below */
    jmp_buf abort;        /* an address error ends the instruction */
} M68k;

/* Implemented by the system the CPU sits in. Addresses are 24 bits; fc is
 * the function code (1 user data, 2 user program, 5 supervisor data,
 * 6 supervisor program, 7 interrupt acknowledge); strobe is 3 for a word,
 * 2 for the upper byte (even address), 1 for the lower (odd). A read
 * returns the 16-bit data bus (a byte in the half its strobe selects); a
 * write puts the byte on both halves. c->cycles counts the access's 4
 * clocks before the call. */
uint16_t m68k_read(M68k *c, uint32_t addr, unsigned fc, unsigned strobe);
void m68k_write(M68k *c, uint32_t addr, uint16_t data, unsigned fc, unsigned strobe);
void m68k_idle(M68k *c, unsigned clocks);  /* internal clocks: no bus access */
/* An access the CPU starts and drops (an address error): no address strobe,
 * so nothing is read or written; 4 clocks. */
void m68k_aborted(M68k *c, uint32_t addr, unsigned fc, unsigned strobe, int write);
/* Interrupt acknowledge: the vector number for the level, or -1 for the
 * autovector (24 + level). */
int m68k_iack(M68k *c, int level);
/* RESET asserts the reset line for the rest of the system. */
void m68k_reset_out(M68k *c);

/* SR and its stack pointer swap (S selects ssp or usp as a[7]). */
void m68k_set_sr(M68k *c, uint16_t sr);
/* Power-on / RESET line: SSP and PC from the vectors at 0 and 4. */
void m68k_reset(M68k *c);
/* One instruction (or an exception, or an interrupt, or 4 clocks stopped). */
void m68k_step(M68k *c);

#endif
