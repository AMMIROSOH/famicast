/* Instruction semantics shared by the interpreter and (later) recompiled
 * code: bus cycles, register pairs, ALU and flags. Flag rules: Pan Docs "CPU
 * instruction set" and gbdev's opcode table (docs/07); all checked against
 * SingleStepTests sm83. */
#ifndef GB_CPU_OPS_H
#define GB_CPU_OPS_H

#include "cpu.h"

static inline uint8_t rd(GbCpu *c, uint16_t a) { c->cycles++; return gb_read(c, a); }
static inline void wr(GbCpu *c, uint16_t a, uint8_t v) { c->cycles++; gb_write(c, a, v); }
static inline void idle(GbCpu *c) { c->cycles++; gb_idle(c); }
/* recompiled code: a code byte read as the CPU does (the cycle, the bus),
 * the value known when it was recompiled.
 * shortcut: ROM code run during OAM DMA from ROM would read the DMA's byte;
 * games wait in HRAM then (gb_headless --diff would show it). */
static inline uint8_t fetch_k(GbCpu *c, uint16_t a, uint8_t k) { c->cycles++; (void)gb_read(c, a); return k; }

static inline uint16_t bc(const GbCpu *c) { return (uint16_t)(c->b << 8 | c->c); }
static inline uint16_t de(const GbCpu *c) { return (uint16_t)(c->d << 8 | c->e); }
static inline uint16_t hl(const GbCpu *c) { return (uint16_t)(c->h << 8 | c->l); }
static inline void set_bc(GbCpu *c, uint16_t v) { c->b = (uint8_t)(v >> 8), c->c = (uint8_t)v; }
static inline void set_de(GbCpu *c, uint16_t v) { c->d = (uint8_t)(v >> 8), c->e = (uint8_t)v; }
static inline void set_hl(GbCpu *c, uint16_t v) { c->h = (uint8_t)(v >> 8), c->l = (uint8_t)v; }

static inline uint8_t zf(uint8_t v) { return v ? 0 : FLAG_Z; }

/* 8-bit ALU on A */
static inline void op_add(GbCpu *c, uint8_t v, int carry) {
    unsigned r = c->a + v + carry;
    c->f = (uint8_t)(zf((uint8_t)r) | ((c->a & 0xF) + (v & 0xF) + carry > 0xF ? FLAG_H : 0) | (r > 0xFF ? FLAG_C : 0));
    c->a = (uint8_t)r;
}
static inline uint8_t sub8(GbCpu *c, uint8_t v, int carry) {  /* A - v - carry; flags set, A kept */
    int r = c->a - v - carry;
    c->f = (uint8_t)(zf((uint8_t)r) | FLAG_N | ((c->a & 0xF) < (v & 0xF) + carry ? FLAG_H : 0) | (r < 0 ? FLAG_C : 0));
    return (uint8_t)r;
}
static inline void op_ADD(GbCpu *c, uint8_t v) { op_add(c, v, 0); }
static inline void op_ADC(GbCpu *c, uint8_t v) { op_add(c, v, (c->f & FLAG_C) != 0); }
static inline void op_SUB(GbCpu *c, uint8_t v) { c->a = sub8(c, v, 0); }
static inline void op_SBC(GbCpu *c, uint8_t v) { c->a = sub8(c, v, (c->f & FLAG_C) != 0); }
static inline void op_AND(GbCpu *c, uint8_t v) { c->a &= v; c->f = (uint8_t)(zf(c->a) | FLAG_H); }
static inline void op_XOR(GbCpu *c, uint8_t v) { c->a ^= v; c->f = zf(c->a); }
static inline void op_OR(GbCpu *c, uint8_t v) { c->a |= v; c->f = zf(c->a); }
static inline void op_CP(GbCpu *c, uint8_t v) { sub8(c, v, 0); }

static inline uint8_t op_INC(GbCpu *c, uint8_t v) {
    uint8_t r = (uint8_t)(v + 1);
    c->f = (uint8_t)((c->f & FLAG_C) | zf(r) | ((v & 0xF) == 0xF ? FLAG_H : 0));
    return r;
}
static inline uint8_t op_DEC(GbCpu *c, uint8_t v) {
    uint8_t r = (uint8_t)(v - 1);
    c->f = (uint8_t)((c->f & FLAG_C) | zf(r) | FLAG_N | ((v & 0xF) == 0 ? FLAG_H : 0));
    return r;
}

/* ADD HL,rr: Z kept, H from bit 11, C from bit 15 */
static inline void op_add_hl(GbCpu *c, uint16_t v) {
    unsigned r = hl(c) + v;
    c->f = (uint8_t)((c->f & FLAG_Z) | ((hl(c) & 0xFFF) + (v & 0xFFF) > 0xFFF ? FLAG_H : 0) | (r > 0xFFFF ? FLAG_C : 0));
    set_hl(c, (uint16_t)r);
}
/* SP + signed e (ADD SP,e; LD HL,SP+e): H and C from the low byte, unsigned */
static inline uint16_t op_sp_e(GbCpu *c, uint8_t e) {
    c->f = (uint8_t)(((c->sp & 0xF) + (e & 0xF) > 0xF ? FLAG_H : 0) | ((c->sp & 0xFF) + e > 0xFF ? FLAG_C : 0));
    return (uint16_t)(c->sp + (int8_t)e);
}

static inline void op_DAA(GbCpu *c) {
    uint8_t adj = 0, carry = c->f & FLAG_C;
    int sub = c->f & FLAG_N;
    if ((c->f & FLAG_H) || (!sub && (c->a & 0xF) > 9)) adj |= 0x06;
    if (carry || (!sub && c->a > 0x99)) adj |= 0x60, carry = FLAG_C;
    c->a = (uint8_t)(sub ? c->a - adj : c->a + adj);
    c->f = (uint8_t)(zf(c->a) | (c->f & FLAG_N) | carry);
}

/* rotates and shifts (CB 00-3F); RLCA/RRCA/RLA/RRA are these with Z clear */
static inline uint8_t shift_flags(GbCpu *c, uint8_t r, int carry) {
    c->f = (uint8_t)(zf(r) | (carry ? FLAG_C : 0));
    return r;
}
static inline uint8_t op_RLC(GbCpu *c, uint8_t v) { return shift_flags(c, (uint8_t)(v << 1 | v >> 7), v >> 7); }
static inline uint8_t op_RRC(GbCpu *c, uint8_t v) { return shift_flags(c, (uint8_t)(v >> 1 | v << 7), v & 1); }
static inline uint8_t op_RL(GbCpu *c, uint8_t v) { return shift_flags(c, (uint8_t)(v << 1 | ((c->f & FLAG_C) != 0)), v >> 7); }
static inline uint8_t op_RR(GbCpu *c, uint8_t v) { return shift_flags(c, (uint8_t)(v >> 1 | ((c->f & FLAG_C) ? 0x80 : 0)), v & 1); }
static inline uint8_t op_SLA(GbCpu *c, uint8_t v) { return shift_flags(c, (uint8_t)(v << 1), v >> 7); }
static inline uint8_t op_SRA(GbCpu *c, uint8_t v) { return shift_flags(c, (uint8_t)(v >> 1 | (v & 0x80)), v & 1); }
static inline uint8_t op_SWAP(GbCpu *c, uint8_t v) { return shift_flags(c, (uint8_t)(v << 4 | v >> 4), 0); }
static inline uint8_t op_SRL(GbCpu *c, uint8_t v) { return shift_flags(c, (uint8_t)(v >> 1), v & 1); }
static inline void op_BIT(GbCpu *c, int bit, uint8_t v) {
    c->f = (uint8_t)((c->f & FLAG_C) | FLAG_H | zf(v & (1 << bit)));
}

#endif
