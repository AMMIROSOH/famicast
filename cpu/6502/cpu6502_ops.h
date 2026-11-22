/* Instruction semantics shared by the interpreter and recompiled code.
 * Flag rules: nes/docs/6502-traps.md. Unofficial opcodes: nesdev wiki
 * "CPU unofficial opcodes"; all checked against SingleStepTests. */
#ifndef CPU6502_OPS_H
#define CPU6502_OPS_H

#include "cpu6502.h"

/* Recompiled code is built with CPU_OPS_OUTLINE: one out-of-line copy of the
 * bus helpers per file keeps thousands of blocks small (CMakeLists.txt). */
#if defined(CPU_OPS_OUTLINE) && defined(_MSC_VER)
#define BUS_INLINE static __declspec(noinline)
#elif defined(CPU_OPS_OUTLINE)
#define BUS_INLINE static __attribute__((noinline))
#else
#define BUS_INLINE static inline
#endif

/* After every cycle the interrupt lines are sampled; an instruction ends by
 * checking the sample from its second-to-last cycle, as the 6502 does
 * (nesdev "CPU interrupts"). This also delays CLI/SEI/PLP by one instruction. */
static inline void poll_interrupts(Cpu *c) {
    c->poll_prev = c->poll;
    c->poll = c->nmi || (c->irq && !(c->p & FLAG_I));
}
/* One bus cycle. Mapped pages (RAM, ROM) are accessed inline while the bus
 * clock has room; everything else, and the cycle where the bus must catch
 * up, goes through cpu_read/cpu_write. Both paths do the same thing. */
BUS_INLINE uint8_t rd(Cpu *c, uint16_t a) {
    c->cycles++;
    const uint8_t *m = c->read_map[a >> 11];
    int32_t due = c->bus_pending + c->bus_step;
    uint8_t v;
    if (m && due < c->bus_budget) {
        c->bus_pending = due;
        v = c->open_bus = m[a & 0x7FF];
    } else {
        v = cpu_read(c, a);
    }
    poll_interrupts(c);
    return v;
}
BUS_INLINE void wr(Cpu *c, uint16_t a, uint8_t v) {
    c->cycles++;
    uint8_t *m = c->write_map[a >> 11];
    int32_t due = c->bus_pending + c->bus_step;
    if (m && due < c->bus_budget) {
        c->bus_pending = due;
        c->open_bus = m[a & 0x7FF] = v;
    } else {
        cpu_write(c, a, v);
    }
    poll_interrupts(c);
}
/* A bus cycle reading a byte known at compile time (code bytes in ROM, used
 * by recompiled code): same timing and side effects as rd(), no load. */
BUS_INLINE uint8_t fetch_k(Cpu *c, uint16_t a, uint8_t k) {
    c->cycles++;
    int32_t due = c->bus_pending + c->bus_step;
    if (c->read_map[a >> 11] && due < c->bus_budget) {
        c->bus_pending = due;
        c->open_bus = k;
    } else {
        (void)cpu_read(c, a);
    }
    poll_interrupts(c);
    return k;
}
static inline void push(Cpu *c, uint8_t v) { wr(c, 0x100 | c->s, v); c->s--; }
static inline uint8_t pull(Cpu *c) { c->s++; return rd(c, 0x100 | c->s); }

static inline void nz(Cpu *c, uint8_t v) {
    c->p = (uint8_t)((c->p & ~(FLAG_Z | FLAG_N)) | (v ? 0 : FLAG_Z) | (v & FLAG_N));
}
static inline void set_c(Cpu *c, int on) { c->p = (uint8_t)((c->p & ~FLAG_C) | (on ? FLAG_C : 0)); }

/* ---- reads: consume an operand value ---- */
static inline void op_LDA(Cpu *c, uint8_t v) { c->a = v; nz(c, v); }
static inline void op_LDX(Cpu *c, uint8_t v) { c->x = v; nz(c, v); }
static inline void op_LDY(Cpu *c, uint8_t v) { c->y = v; nz(c, v); }
static inline void op_AND(Cpu *c, uint8_t v) { c->a &= v; nz(c, c->a); }
static inline void op_ORA(Cpu *c, uint8_t v) { c->a |= v; nz(c, c->a); }
static inline void op_EOR(Cpu *c, uint8_t v) { c->a ^= v; nz(c, c->a); }
static inline void op_ADC(Cpu *c, uint8_t v) {
    unsigned s = c->a + v + (c->p & FLAG_C);  /* 2A03: no decimal mode */
    c->p = (uint8_t)((c->p & ~(FLAG_C | FLAG_V)) | (s > 0xFF ? FLAG_C : 0) |
                     ((~(c->a ^ v) & (c->a ^ s) & 0x80) ? FLAG_V : 0));
    c->a = (uint8_t)s;
    nz(c, c->a);
}
static inline void op_SBC(Cpu *c, uint8_t v) { op_ADC(c, (uint8_t)~v); }
static inline void compare(Cpu *c, uint8_t r, uint8_t v) { set_c(c, r >= v); nz(c, (uint8_t)(r - v)); }
static inline void op_CMP(Cpu *c, uint8_t v) { compare(c, c->a, v); }
static inline void op_CPX(Cpu *c, uint8_t v) { compare(c, c->x, v); }
static inline void op_CPY(Cpu *c, uint8_t v) { compare(c, c->y, v); }
static inline void op_BIT(Cpu *c, uint8_t v) {
    c->p = (uint8_t)((c->p & ~(FLAG_Z | FLAG_V | FLAG_N)) | ((c->a & v) ? 0 : FLAG_Z) |
                     (v & (FLAG_V | FLAG_N)));
}
static inline void op_NOP(Cpu *c, uint8_t v) { (void)c; (void)v; }
static inline void op_LAX(Cpu *c, uint8_t v) { c->a = c->x = v; nz(c, v); }
static inline void op_LAS(Cpu *c, uint8_t v) { c->a = c->x = c->s = v & c->s; nz(c, c->a); }
static inline void op_ANC(Cpu *c, uint8_t v) { op_AND(c, v); set_c(c, c->a & 0x80); }
static inline void op_ALR(Cpu *c, uint8_t v) {
    c->a &= v; set_c(c, c->a & 1); c->a >>= 1; nz(c, c->a);
}
static inline void op_ARR(Cpu *c, uint8_t v) {
    c->a = (uint8_t)(((c->a & v) >> 1) | ((c->p & FLAG_C) << 7));
    nz(c, c->a);
    set_c(c, c->a & 0x40);
    c->p = (uint8_t)((c->p & ~FLAG_V) | (((c->a >> 6) ^ (c->a >> 5)) & 1 ? FLAG_V : 0));
}
static inline void op_AXS(Cpu *c, uint8_t v) {
    uint8_t t = c->a & c->x; set_c(c, t >= v); c->x = (uint8_t)(t - v); nz(c, c->x);
}
/* $8B XAA and $AB LXA are unstable on hardware (result depends on the chip).
 * XAA: magic 0xEE, matches SingleStepTests. LXA: A = X = operand (magic 0xFF),
 * matches blargg instr_test-v5, which was verified on a real NES; SingleStepTests
 * assumes 0xEE here, see nes/docs/verification-log.md. */
static inline void op_XAA(Cpu *c, uint8_t v) { c->a = (c->a | 0xEE) & c->x & v; nz(c, c->a); }
static inline void op_LXA(Cpu *c, uint8_t v) { c->a = c->x = v; nz(c, c->a); }

/* ---- read-modify-write: return the new memory value ---- */
static inline uint8_t op_ASL(Cpu *c, uint8_t v) { set_c(c, v & 0x80); v <<= 1; nz(c, v); return v; }
static inline uint8_t op_LSR(Cpu *c, uint8_t v) { set_c(c, v & 1); v >>= 1; nz(c, v); return v; }
static inline uint8_t op_ROL(Cpu *c, uint8_t v) {
    uint8_t r = (uint8_t)((v << 1) | (c->p & FLAG_C)); set_c(c, v & 0x80); nz(c, r); return r;
}
static inline uint8_t op_ROR(Cpu *c, uint8_t v) {
    uint8_t r = (uint8_t)((v >> 1) | ((c->p & FLAG_C) << 7)); set_c(c, v & 1); nz(c, r); return r;
}
static inline uint8_t op_INC(Cpu *c, uint8_t v) { v++; nz(c, v); return v; }
static inline uint8_t op_DEC(Cpu *c, uint8_t v) { v--; nz(c, v); return v; }
static inline uint8_t op_SLO(Cpu *c, uint8_t v) { v = op_ASL(c, v); op_ORA(c, v); return v; }
static inline uint8_t op_RLA(Cpu *c, uint8_t v) { v = op_ROL(c, v); op_AND(c, v); return v; }
static inline uint8_t op_SRE(Cpu *c, uint8_t v) { v = op_LSR(c, v); op_EOR(c, v); return v; }
static inline uint8_t op_RRA(Cpu *c, uint8_t v) { v = op_ROR(c, v); op_ADC(c, v); return v; }
static inline uint8_t op_DCP(Cpu *c, uint8_t v) { v = op_DEC(c, v); op_CMP(c, v); return v; }
static inline uint8_t op_ISB(Cpu *c, uint8_t v) { v = op_INC(c, v); op_SBC(c, v); return v; }

#endif
