/* Z80 core: state, bus interface, interpreter.
 * Timing is counted in T-states: every bus access and every internal cycle
 * adds its T-states to z->tstates before it happens (M1 4, memory 3, port 4;
 * checked T-state by T-state against SingleStepTests z80,
 * systems/genesis/docs/verification-log.md). */
#ifndef Z80_CPU_H
#define Z80_CPU_H

#include <stdint.h>

/* F */
enum { Z80_FLAG_S = 0x80, Z80_FLAG_Z = 0x40, Z80_FLAG_Y = 0x20, Z80_FLAG_H = 0x10,
       Z80_FLAG_X = 0x08, Z80_FLAG_PV = 0x04, Z80_FLAG_N = 0x02, Z80_FLAG_C = 0x01 };

typedef struct Z80 {
    uint16_t pc, sp;
    uint16_t wz;         /* MEMPTR: the hidden register behind bits 3 and 5 of BIT n,(HL) and others */
    uint16_t addr;       /* the last address on the address bus (internal cycles keep it) */
    uint8_t a, f, b, c, d, e, h, l;
    uint8_t ixh, ixl, iyh, iyl;
    uint16_t af_, bc_, de_, hl_;  /* the shadow set */
    uint8_t i, r;        /* r: 7 bits count, bit 7 is only what LD R,A wrote */
    uint8_t iff1, iff2, im;
    uint8_t ei;          /* EI ran: no interrupt is accepted before the next instruction ends */
    uint8_t q;           /* F if the last instruction wrote it, else 0 (SCF and CCF use it) */
    uint8_t qprev;       /* q of the instruction before, for the one running */
    uint8_t p;           /* the last instruction was LD A,I or LD A,R (an interrupt then clears P/V) */
    uint8_t halted;      /* HALT: waiting for an interrupt or NMI */
    uint64_t tstates;    /* T-states since power-on */
    void *bus;           /* owner's context for the z80_* bus functions */
} Z80;

/* Implemented by whatever bus is linked with the core (Genesis sound bus, test
 * bus). The T-states of each call are already added to z->tstates; the
 * system adds its own wait states to it. */
uint8_t z80_fetch(Z80 *z, uint16_t pc);              /* opcode fetch (M1, 4 T-states). The refresh address is z->i << 8 | z->r (r is not yet counted) */
uint8_t z80_read(Z80 *z, uint16_t addr);             /* memory read, 3 */
void z80_write(Z80 *z, uint16_t addr, uint8_t v);    /* memory write, 3 */
uint8_t z80_in(Z80 *z, uint16_t port);               /* port read, 4 (the address bus carries the 16 bits) */
void z80_out(Z80 *z, uint16_t port, uint8_t v);      /* port write, 4 */
void z80_idle(Z80 *z, uint16_t addr, unsigned n);    /* n T-states without a bus access, addr on the address bus */
uint8_t z80_irq_ack(Z80 *z);                         /* maskable interrupt: the byte the device puts on the data bus (RST 38 = $FF) */

void z80_reset(Z80 *z);
/* One instruction (or one NOP of a halted CPU). */
void z80_step(Z80 *z);
/* A maskable interrupt: the system calls it while INT is low. Returns the
 * T-states it took, 0 when not accepted (IFF1 off, or right after EI). */
unsigned z80_interrupt(Z80 *z);
/* Non-maskable interrupt (the falling edge of NMI): always accepted. Returns the T-states. */
unsigned z80_nmi(Z80 *z);

#endif
