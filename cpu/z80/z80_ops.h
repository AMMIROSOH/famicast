/* Instruction semantics shared by the interpreter and (later) recompiled
 * code: bus cycles, register pairs, ALU and flags, including the undocumented
 * bits 3 and 5 of F and the Q register. Flag rules: Sean Young, "The
 * Undocumented Z80 Documented", Zilog's Z80 manual (docs/sources.md); all
 * checked against SingleStepTests z80. */
#ifndef Z80_OPS_H
#define Z80_OPS_H

#include "z80_cpu.h"

/* rd and fetch_k are the names core/recomp/template.h's markers expand to */
static inline uint8_t rd(Z80 *c, uint16_t a) { c->tstates += 3; c->addr = a; return z80_read(c, a); }
static inline void z80_wr(Z80 *c, uint16_t a, uint8_t v) { c->tstates += 3; c->addr = a; z80_write(c, a, v); }
static inline uint8_t z80_inp(Z80 *c, uint16_t p) { c->tstates += 4; c->addr = p; return z80_in(c, p); }
static inline void z80_outp(Z80 *c, uint16_t p, uint8_t v) { c->tstates += 4; c->addr = p; z80_out(c, p, v); }
/* n T-states on the address bus as it was (after an M1: the refresh address) */
static inline void z80_tick(Z80 *c, unsigned n) { c->tstates += n; z80_idle(c, c->addr, n); }
/* recompiled code: a code byte read as the CPU does (the cycles, the bus), the value known when it was recompiled. */
static inline uint8_t fetch_k(Z80 *c, uint16_t a, uint8_t k) { (void)rd(c, a); return k; }

/* M1: the opcode at PC, then the refresh (R counts 7 bits) */
static inline uint8_t z80_m1(Z80 *c) {
    c->tstates += 4;
    c->addr = (uint16_t)(c->i << 8 | c->r);
    uint8_t op = z80_fetch(c, c->pc++);
    c->r = (uint8_t)((c->r & 0x80) | ((c->r + 1) & 0x7F));
    return op;
}

static inline uint16_t z80_bc(const Z80 *c) { return (uint16_t)(c->b << 8 | c->c); }
static inline uint16_t z80_de(const Z80 *c) { return (uint16_t)(c->d << 8 | c->e); }
static inline uint16_t z80_hl(const Z80 *c) { return (uint16_t)(c->h << 8 | c->l); }
static inline uint16_t z80_ix(const Z80 *c) { return (uint16_t)(c->ixh << 8 | c->ixl); }
static inline uint16_t z80_iy(const Z80 *c) { return (uint16_t)(c->iyh << 8 | c->iyl); }
static inline void z80_set_bc(Z80 *c, uint16_t v) { c->b = (uint8_t)(v >> 8), c->c = (uint8_t)v; }
static inline void z80_set_de(Z80 *c, uint16_t v) { c->d = (uint8_t)(v >> 8), c->e = (uint8_t)v; }
static inline void z80_set_hl(Z80 *c, uint16_t v) { c->h = (uint8_t)(v >> 8), c->l = (uint8_t)v; }
static inline void z80_set_ix(Z80 *c, uint16_t v) { c->ixh = (uint8_t)(v >> 8), c->ixl = (uint8_t)v; }
static inline void z80_set_iy(Z80 *c, uint16_t v) { c->iyh = (uint8_t)(v >> 8), c->iyl = (uint8_t)v; }
static inline uint16_t z80_af(const Z80 *c) { return (uint16_t)(c->a << 8 | c->f); }

/* every write to F goes through here: Q remembers it for SCF and CCF */
static inline void z80_setf(Z80 *c, uint8_t f) { c->f = f; c->q = f; }

static inline int z80_odd(unsigned v) {  /* an odd number of bits set */
    v ^= v >> 4; v ^= v >> 2; v ^= v >> 1;
    return v & 1;
}
/* S, Z, bits 5 and 3, and P (even parity) of a result */
static inline uint8_t z80_szp(uint8_t v) {
    return (uint8_t)((v & (Z80_FLAG_S | Z80_FLAG_Y | Z80_FLAG_X)) | (v ? 0 : Z80_FLAG_Z) | (z80_odd(v) ? 0 : Z80_FLAG_PV));
}
static inline uint8_t z80_sz(uint8_t v) {  /* S, Z, bits 5 and 3 */
    return (uint8_t)((v & (Z80_FLAG_S | Z80_FLAG_Y | Z80_FLAG_X)) | (v ? 0 : Z80_FLAG_Z));
}
#define Z80_SZPC (Z80_FLAG_S | Z80_FLAG_Z | Z80_FLAG_PV)

/* 8-bit ALU on A */
static inline uint8_t z80_add8(Z80 *c, uint8_t x, uint8_t y, int cy) {  /* x + y + cy, flags set */
    unsigned r = x + y + cy;
    z80_setf(c, (uint8_t)(z80_sz((uint8_t)r) | ((x ^ y ^ r) & Z80_FLAG_H) | ((~(x ^ y) & (x ^ r) & 0x80) ? Z80_FLAG_PV : 0) | (r >> 8)));
    return (uint8_t)r;
}
static inline uint8_t z80_sub8(Z80 *c, uint8_t x, uint8_t y, int cy) {  /* x - y - cy, flags set */
    unsigned r = (unsigned)x - y - cy;
    z80_setf(c, (uint8_t)(z80_sz((uint8_t)r) | ((x ^ y ^ r) & Z80_FLAG_H) | (((x ^ y) & (x ^ r) & 0x80) ? Z80_FLAG_PV : 0) |
                          Z80_FLAG_N | ((r >> 8) & 1)));
    return (uint8_t)r;
}
static inline void z80_op_ADD(Z80 *c, uint8_t v) { c->a = z80_add8(c, c->a, v, 0); }
static inline void z80_op_ADC(Z80 *c, uint8_t v) { c->a = z80_add8(c, c->a, v, c->f & Z80_FLAG_C); }
static inline void z80_op_SUB(Z80 *c, uint8_t v) { c->a = z80_sub8(c, c->a, v, 0); }
static inline void z80_op_SBC(Z80 *c, uint8_t v) { c->a = z80_sub8(c, c->a, v, c->f & Z80_FLAG_C); }
static inline void z80_op_AND(Z80 *c, uint8_t v) { c->a &= v; z80_setf(c, (uint8_t)(z80_szp(c->a) | Z80_FLAG_H)); }
static inline void z80_op_XOR(Z80 *c, uint8_t v) { c->a ^= v; z80_setf(c, z80_szp(c->a)); }
static inline void z80_op_OR(Z80 *c, uint8_t v) { c->a |= v; z80_setf(c, z80_szp(c->a)); }
static inline void z80_op_CP(Z80 *c, uint8_t v) {  /* bits 3 and 5 come from the operand */
    z80_sub8(c, c->a, v, 0);
    z80_setf(c, (uint8_t)((c->f & ~(Z80_FLAG_Y | Z80_FLAG_X)) | (v & (Z80_FLAG_Y | Z80_FLAG_X))));
}

static inline uint8_t z80_op_INC(Z80 *c, uint8_t v) {
    uint8_t r = (uint8_t)(v + 1);
    z80_setf(c, (uint8_t)((c->f & Z80_FLAG_C) | z80_sz(r) | ((v & 0xF) == 0xF ? Z80_FLAG_H : 0) | (v == 0x7F ? Z80_FLAG_PV : 0)));
    return r;
}
static inline uint8_t z80_op_DEC(Z80 *c, uint8_t v) {
    uint8_t r = (uint8_t)(v - 1);
    z80_setf(c, (uint8_t)((c->f & Z80_FLAG_C) | z80_sz(r) | Z80_FLAG_N | ((v & 0xF) == 0 ? Z80_FLAG_H : 0) | (v == 0x80 ? Z80_FLAG_PV : 0)));
    return r;
}
static inline void z80_op_NEG(Z80 *c) { c->a = z80_sub8(c, 0, c->a, 0); }

/* 16-bit adds: ADD keeps S, Z, P/V; ADC and SBC set all; bits 3, 5, and H are those of the high byte */
static inline uint16_t z80_add16(Z80 *c, uint16_t x, uint16_t y) {
    unsigned r = (unsigned)x + y;
    z80_setf(c, (uint8_t)((c->f & Z80_SZPC) | ((r >> 8) & (Z80_FLAG_Y | Z80_FLAG_X)) | (((x ^ y ^ r) >> 8) & Z80_FLAG_H) | (r >> 16)));
    return (uint16_t)r;
}
static inline uint16_t z80_adc16(Z80 *c, uint16_t x, uint16_t y) {
    unsigned r = (unsigned)x + y + (c->f & Z80_FLAG_C);
    z80_setf(c, (uint8_t)(((r >> 8) & (Z80_FLAG_S | Z80_FLAG_Y | Z80_FLAG_X)) | ((r & 0xFFFF) ? 0 : Z80_FLAG_Z) |
                          (((x ^ y ^ r) >> 8) & Z80_FLAG_H) | ((~(x ^ y) & (x ^ r) & 0x8000) ? Z80_FLAG_PV : 0) | (r >> 16)));
    return (uint16_t)r;
}
static inline uint16_t z80_sbc16(Z80 *c, uint16_t x, uint16_t y) {
    unsigned r = (unsigned)x - y - (c->f & Z80_FLAG_C);
    z80_setf(c, (uint8_t)(((r >> 8) & (Z80_FLAG_S | Z80_FLAG_Y | Z80_FLAG_X)) | ((r & 0xFFFF) ? 0 : Z80_FLAG_Z) |
                          (((x ^ y ^ r) >> 8) & Z80_FLAG_H) | (((x ^ y) & (x ^ r) & 0x8000) ? Z80_FLAG_PV : 0) | Z80_FLAG_N | ((r >> 16) & 1)));
    return (uint16_t)r;
}

static inline void z80_op_DAA(Z80 *c) {
    uint8_t a = c->a, adj = 0, cy = c->f & Z80_FLAG_C, n = c->f & Z80_FLAG_N;
    if ((c->f & Z80_FLAG_H) || (a & 0xF) > 9) adj |= 0x06;
    if (cy || a > 0x99) adj |= 0x60, cy = Z80_FLAG_C;
    uint8_t h = n ? ((c->f & Z80_FLAG_H) && (a & 0xF) < 6) : ((a & 0xF) > 9);
    c->a = (uint8_t)(n ? a - adj : a + adj);
    z80_setf(c, (uint8_t)(z80_szp(c->a) | n | (h ? Z80_FLAG_H : 0) | cy));
}
static inline void z80_op_CPL(Z80 *c) {
    c->a = (uint8_t)~c->a;
    z80_setf(c, (uint8_t)((c->f & (Z80_SZPC | Z80_FLAG_C)) | Z80_FLAG_H | Z80_FLAG_N | (c->a & (Z80_FLAG_Y | Z80_FLAG_X))));
}
/* SCF and CCF: bits 3 and 5 are A's, ORed with F's when the last instruction did not write F (Q) */
static inline uint8_t z80_xy_scf(const Z80 *c) { return (uint8_t)(((c->qprev ^ c->f) | c->a) & (Z80_FLAG_Y | Z80_FLAG_X)); }
static inline void z80_op_SCF(Z80 *c) { z80_setf(c, (uint8_t)((c->f & Z80_SZPC) | Z80_FLAG_C | z80_xy_scf(c))); }
static inline void z80_op_CCF(Z80 *c) {
    z80_setf(c, (uint8_t)((c->f & Z80_SZPC) | ((c->f & Z80_FLAG_C) ? Z80_FLAG_H : Z80_FLAG_C) | z80_xy_scf(c)));
}

/* rotates and shifts (CB 00-3F, SLL undocumented); RLCA/RRCA/RLA/RRA are the first four with other flags */
static inline uint8_t z80_shift_flags(Z80 *c, uint8_t r, int carry) {
    z80_setf(c, (uint8_t)(z80_szp(r) | (carry ? Z80_FLAG_C : 0)));
    return r;
}
static inline uint8_t z80_op_RLC(Z80 *c, uint8_t v) { return z80_shift_flags(c, (uint8_t)(v << 1 | v >> 7), v >> 7); }
static inline uint8_t z80_op_RRC(Z80 *c, uint8_t v) { return z80_shift_flags(c, (uint8_t)(v >> 1 | v << 7), v & 1); }
static inline uint8_t z80_op_RL(Z80 *c, uint8_t v) { return z80_shift_flags(c, (uint8_t)(v << 1 | (c->f & Z80_FLAG_C)), v >> 7); }
static inline uint8_t z80_op_RR(Z80 *c, uint8_t v) { return z80_shift_flags(c, (uint8_t)(v >> 1 | ((c->f & Z80_FLAG_C) << 7)), v & 1); }
static inline uint8_t z80_op_SLA(Z80 *c, uint8_t v) { return z80_shift_flags(c, (uint8_t)(v << 1), v >> 7); }
static inline uint8_t z80_op_SRA(Z80 *c, uint8_t v) { return z80_shift_flags(c, (uint8_t)(v >> 1 | (v & 0x80)), v & 1); }
static inline uint8_t z80_op_SLL(Z80 *c, uint8_t v) { return z80_shift_flags(c, (uint8_t)(v << 1 | 1), v >> 7); }
static inline uint8_t z80_op_SRL(Z80 *c, uint8_t v) { return z80_shift_flags(c, (uint8_t)(v >> 1), v & 1); }
/* the accumulator forms keep S, Z, P/V, clear H and N, and take bits 3 and 5 from the result */
static inline uint8_t z80_acc_rot(Z80 *c, uint8_t r, int carry) {
    z80_setf(c, (uint8_t)((c->f & Z80_SZPC) | (r & (Z80_FLAG_Y | Z80_FLAG_X)) | (carry ? Z80_FLAG_C : 0)));
    return r;
}
static inline void z80_op_RLCA(Z80 *c) { c->a = z80_acc_rot(c, (uint8_t)(c->a << 1 | c->a >> 7), c->a >> 7); }
static inline void z80_op_RRCA(Z80 *c) { c->a = z80_acc_rot(c, (uint8_t)(c->a >> 1 | c->a << 7), c->a & 1); }
static inline void z80_op_RLA(Z80 *c) { c->a = z80_acc_rot(c, (uint8_t)(c->a << 1 | (c->f & Z80_FLAG_C)), c->a >> 7); }
static inline void z80_op_RRA(Z80 *c) { c->a = z80_acc_rot(c, (uint8_t)(c->a >> 1 | ((c->f & Z80_FLAG_C) << 7)), c->a & 1); }

/* BIT n: Z and P/V from the bit, S from bit 7, H set, C kept; bits 3 and 5 of `xy` */
static inline void z80_op_BIT(Z80 *c, int bit, uint8_t v, uint8_t xy) {
    uint8_t f = (uint8_t)((c->f & Z80_FLAG_C) | Z80_FLAG_H | (xy & (Z80_FLAG_Y | Z80_FLAG_X)));
    if (!(v & (1 << bit))) f |= Z80_FLAG_Z | Z80_FLAG_PV;
    else if (bit == 7) f |= Z80_FLAG_S;
    z80_setf(c, f);
}

/* IN r,(C) and its flags */
static inline uint8_t z80_in_flags(Z80 *c, uint8_t v) { z80_setf(c, (uint8_t)(z80_szp(v) | (c->f & Z80_FLAG_C))); return v; }
/* LD A,I / LD A,R: P/V is IFF2 */
static inline void z80_ld_a_ir(Z80 *c, uint8_t v) {
    c->a = v;
    c->p = 1;
    z80_setf(c, (uint8_t)(z80_sz(v) | (c->f & Z80_FLAG_C) | (c->iff2 ? Z80_FLAG_PV : 0)));
}
/* RRD / RLD: the A and (HL) nibbles rotate; returns the new (HL) */
static inline uint8_t z80_rxd(Z80 *c, uint8_t m, int left) {
    uint8_t a = c->a, nm;
    if (left) nm = (uint8_t)(m << 4 | (a & 0xF)), c->a = (uint8_t)((a & 0xF0) | m >> 4);
    else nm = (uint8_t)(m >> 4 | a << 4), c->a = (uint8_t)((a & 0xF0) | (m & 0xF));
    z80_setf(c, (uint8_t)(z80_szp(c->a) | (c->f & Z80_FLAG_C)));
    return nm;
}

/* Block instructions. LDI/LDD: v is the byte moved (BC already counted down) */
static inline void z80_blk_ld(Z80 *c, uint8_t v) {
    uint8_t n = (uint8_t)(v + c->a);
    z80_setf(c, (uint8_t)((c->f & (Z80_FLAG_S | Z80_FLAG_Z | Z80_FLAG_C)) | (n & Z80_FLAG_X) | ((n << 4) & Z80_FLAG_Y) |
                          (z80_bc(c) ? Z80_FLAG_PV : 0)));
}
/* CPI/CPD: v is the byte compared (BC already counted down) */
static inline void z80_blk_cp(Z80 *c, uint8_t v) {
    uint8_t r = (uint8_t)(c->a - v), h = (c->a ^ v ^ r) & Z80_FLAG_H, n = (uint8_t)(r - (h ? 1 : 0));
    z80_setf(c, (uint8_t)((c->f & Z80_FLAG_C) | (r & Z80_FLAG_S) | (r ? 0 : Z80_FLAG_Z) | h | Z80_FLAG_N |
                          (n & Z80_FLAG_X) | ((n << 4) & Z80_FLAG_Y) | (z80_bc(c) ? Z80_FLAG_PV : 0)));
}
/* INI/IND/OUTI/OUTD: v the byte, b the counted-down B, k the sum for C and H */
static inline void z80_blk_io(Z80 *c, uint8_t v, unsigned k) {
    z80_setf(c, (uint8_t)(z80_sz(c->b) | ((v & 0x80) ? Z80_FLAG_N : 0) | (k > 255 ? Z80_FLAG_H | Z80_FLAG_C : 0) |
                          (z80_odd((k & 7) ^ c->b) ? 0 : Z80_FLAG_PV)));
}
/* a block instruction repeats: bits 3 and 5 are those of the PC's high byte */
static inline void z80_blk_repeat(Z80 *c, uint16_t pc0) {
    c->wz = (uint16_t)(pc0 + 1);
    z80_setf(c, (uint8_t)((c->f & ~(Z80_FLAG_Y | Z80_FLAG_X)) | ((pc0 >> 8) & (Z80_FLAG_Y | Z80_FLAG_X))));
}
/* ... and INIR/INDR/OTIR/OTDR also change P/V and H by what the counted-down B and the byte are */
static inline void z80_blk_io_repeat(Z80 *c, uint8_t v, uint16_t pc0) {
    uint8_t f;
    z80_blk_repeat(c, pc0);
    f = c->f;
    if (f & Z80_FLAG_C) {
        if (v & 0x80) {
            if (z80_odd((c->b - 1) & 7)) f ^= Z80_FLAG_PV;
            f = (uint8_t)((f & ~Z80_FLAG_H) | ((c->b & 0xF) == 0 ? Z80_FLAG_H : 0));
        } else {
            if (z80_odd((c->b + 1) & 7)) f ^= Z80_FLAG_PV;
            f = (uint8_t)((f & ~Z80_FLAG_H) | ((c->b & 0xF) == 0xF ? Z80_FLAG_H : 0));
        }
    } else if (z80_odd(c->b & 7)) {
        f ^= Z80_FLAG_PV;
    }
    z80_setf(c, f);
}

#endif
