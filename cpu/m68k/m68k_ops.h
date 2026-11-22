/* 68000 instruction helpers for the interpreter's handlers and (later)
 * recompiled code: bus accesses with their alignment checks, effective
 * addresses, flags, BCD, shifts, the multiply and divide timing. The
 * templates (m68k.c) own every use of the PC; these never touch it except
 * the marker expansions (fetch_w, prefetch). Flag and timing rules:
 * Motorola's M68000 PRM and user's manual, checked against SingleStepTests
 * m68000 (docs/sources.md). */
#ifndef M68K_OPS_H
#define M68K_OPS_H

#include "m68k_cpu.h"

/* exceptions (m68k_cpu.c); the address error does not return */
#ifdef _MSC_VER
__declspec(noreturn)
#else
__attribute__((noreturn))
#endif
void m68k_address_error(M68k *c, uint32_t a, int write, int prog, unsigned strobe);
uint32_t m68k_exception(M68k *c, int vector, uint32_t pc, unsigned pre_idle);  /* the new pc */
uint32_t m68k_privilege(M68k *c);  /* privilege violation: the new pc */

static inline unsigned fcd(const M68k *c) { return c->sr & M68K_S ? 5 : 1; }
static inline unsigned fcp(const M68k *c) { return c->sr & M68K_S ? 6 : 2; }
static inline void idle(M68k *c, unsigned n) { c->cycles += n; m68k_idle(c, n); }

static inline uint16_t bus_r(M68k *c, uint32_t a, unsigned fc, unsigned strobe) {
    c->cycles += 4;
    return m68k_read(c, a & 0xFFFFFF, fc, strobe);
}
static inline void bus_w(M68k *c, uint32_t a, uint16_t v, unsigned fc, unsigned strobe) {
    c->cycles += 4;
    m68k_write(c, a & 0xFFFFFF, v, fc, strobe);
}

/* data reads and writes (b, w, l); a word or long at an odd address is an
 * address error. Longs: the high word first, except wr32r (low first: the
 * read-modify-write instructions and MOVE to -(An)). */
static inline uint32_t rd8(M68k *c, uint32_t a) {
    uint16_t v = bus_r(c, a, fcd(c), a & 1 ? 1 : 2);
    return a & 1 ? v & 0xFF : v >> 8;
}
static inline uint32_t rd16(M68k *c, uint32_t a) {
    if (a & 1) m68k_address_error(c, a, 0, 0, 3);
    return bus_r(c, a, fcd(c), 3);
}
static inline uint32_t rd32(M68k *c, uint32_t a) {
    uint32_t h = rd16(c, a);
    return h << 16 | rd16(c, a + 2);
}
/* PC-relative operands are read in program space */
static inline uint32_t rdp8(M68k *c, uint32_t a) {
    uint16_t v = bus_r(c, a, fcp(c), a & 1 ? 1 : 2);
    return a & 1 ? v & 0xFF : v >> 8;
}
static inline uint32_t rdp16(M68k *c, uint32_t a) {
    if (a & 1) m68k_address_error(c, a, 0, 1, 3);
    return bus_r(c, a, fcp(c), 3);
}
static inline uint32_t rdp32(M68k *c, uint32_t a) {
    uint32_t h = rdp16(c, a);
    return h << 16 | rdp16(c, a + 2);
}
static inline void wr8(M68k *c, uint32_t a, uint32_t v) {
    bus_w(c, a, (uint16_t)((v & 0xFF) << 8 | (v & 0xFF)), fcd(c), a & 1 ? 1 : 2);
}
static inline void wr16(M68k *c, uint32_t a, uint32_t v) {
    if (a & 1) m68k_address_error(c, a, 1, 0, 3);
    bus_w(c, a, (uint16_t)v, fcd(c), 3);
}
static inline void wr32(M68k *c, uint32_t a, uint32_t v) {
    wr16(c, a, v >> 16);
    wr16(c, a + 2, v);
}
static inline void wr32r(M68k *c, uint32_t a, uint32_t v) {
    if (a & 1) m68k_address_error(c, a + 2, 1, 0, 3);  /* the fault names the first word written */
    wr16(c, a + 2, v);
    wr16(c, a, v >> 16);
}

/* The prefetch queue (the @W and @N markers): fetch_w hands out irc and
 * reads the next word; prefetch moves irc to ir and reads the next word. */
static inline uint32_t fetch_w(M68k *c) {
    uint16_t v = c->irc;
    c->irc = bus_r(c, c->pc, fcp(c), 3);
    c->pc += 2;
    return v;
}
static inline void prefetch(M68k *c) {
    c->ir = c->irc;
    c->irc = bus_r(c, c->pc, fcp(c), 3);
    c->pc += 2;
}
/* recompiled code: the same bus reads, the code words known */
static inline uint32_t fetch_wk(M68k *c, uint32_t a, uint16_t k) {
    c->irc = bus_r(c, a, fcp(c), 3);
    return k;
}
static inline void prefetch_k(M68k *c, uint32_t a) {
    c->ir = c->irc;
    c->irc = bus_r(c, a, fcp(c), 3);
}
/* A jump: both queue words from the target (an odd target is an address
 * error, raised with the old pc); the new pc. */
static inline uint32_t jump_to(M68k *c, uint32_t t) {
    if (t & 1) m68k_address_error(c, t, 0, 1, 3);
    c->ir = bus_r(c, t, fcp(c), 3);
    c->irc = bus_r(c, t + 2, fcp(c), 3);
    return t + 4;
}
/* the jump's first word, then n idle clocks (jumps that start with it) */
static inline uint32_t jump_first(M68k *c, uint32_t t) {
    if (t & 1) m68k_address_error(c, t, 0, 1, 3);
    c->ir = bus_r(c, t, fcp(c), 3);
    return t + 2;
}

/* JSR's second queue word, after the push */
static inline void jump_rest(M68k *c) {
    c->irc = bus_r(c, c->pc, fcp(c), 3);
    c->pc += 2;
}

/* effective addresses */
static inline uint32_t inc_of(int r, int sz) { return sz == 1 && r == 7 ? 2 : (uint32_t)sz; }
static inline uint32_t pi(M68k *c, int r, int sz) {
    uint32_t a = c->a[r];
    c->a[r] += inc_of(r, sz);
    return a;
}
static inline uint32_t pd(M68k *c, int r, int sz) { return c->a[r] -= inc_of(r, sz); }
/* d8(base, Xn): the brief extension word */
static inline uint32_t ix(const M68k *c, uint32_t base, uint32_t ext) {
    uint32_t x = ext & 0x8000 ? c->a[ext >> 12 & 7] : c->d[ext >> 12 & 7];
    if (!(ext & 0x800)) x = (uint32_t)(int16_t)x;
    return base + (uint32_t)(int8_t)ext + x;
}

/* DBcc: Dn.w - 1; nonzero unless it ran out (-1) */
static inline int dbcc_dec(M68k *c, int r) {
    uint16_t v = (uint16_t)(c->d[r] - 1);
    c->d[r] = (c->d[r] & 0xFFFF0000u) | v;
    return v != 0xFFFF;
}

/* registers by size */
#define M68K_MASK(sz) ((sz) == 1 ? 0xFFu : (sz) == 2 ? 0xFFFFu : 0xFFFFFFFFu)
#define M68K_MSB(sz) ((sz) == 1 ? 0x80u : (sz) == 2 ? 0x8000u : 0x80000000u)
static inline void setd(M68k *c, int r, uint32_t v, int sz) {
    c->d[r] = (c->d[r] & ~M68K_MASK(sz)) | (v & M68K_MASK(sz));
}
static inline uint32_t sext(uint32_t v, int sz) {
    return sz == 1 ? (uint32_t)(int8_t)v : sz == 2 ? (uint32_t)(int16_t)v : v;
}

/* flags */
static inline void ccr(M68k *c, unsigned keep, unsigned set) {
    c->sr = (uint16_t)((c->sr & ~(0x1F & ~keep)) | set);
}
static inline unsigned nz(uint32_t r, int sz) {
    r &= M68K_MASK(sz);
    return (r & M68K_MSB(sz) ? M68K_N : 0) | (r ? 0 : M68K_Z);
}
static inline uint32_t f_logic(M68k *c, uint32_t r, int sz) {
    ccr(c, M68K_X, nz(r, sz));
    return r & M68K_MASK(sz);
}
static inline uint32_t f_add(M68k *c, uint32_t d, uint32_t s, int sz) {
    uint32_t m = M68K_MSB(sz), r = (d + s) & M68K_MASK(sz);
    unsigned f = nz(r, sz) | (~(d ^ s) & (d ^ r) & m ? M68K_V : 0);
    if (((s & d) | (~r & (s | d))) & m) f |= M68K_C | M68K_X;
    ccr(c, 0, f);
    return r;
}
static inline uint32_t f_sub(M68k *c, uint32_t d, uint32_t s, int sz, int keep_x) {
    uint32_t m = M68K_MSB(sz), r = (d - s) & M68K_MASK(sz);
    unsigned f = nz(r, sz) | ((d ^ s) & (d ^ r) & m ? M68K_V : 0);
    if (((s & ~d) | (r & ~d) | (s & r)) & m) f |= keep_x ? M68K_C : M68K_C | M68K_X;
    ccr(c, keep_x ? M68K_X : 0, f);
    return r;
}
/* ADDX/SUBX/NEGX: X in, Z only cleared */
static inline uint32_t f_addx(M68k *c, uint32_t d, uint32_t s, int sz) {
    uint32_t m = M68K_MSB(sz), x = c->sr >> 4 & 1, r = (d + s + x) & M68K_MASK(sz);
    unsigned f = (r & m ? M68K_N : 0) | (~(d ^ s) & (d ^ r) & m ? M68K_V : 0) | (r ? 0 : c->sr & M68K_Z);
    if (((s & d) | (~r & (s | d))) & m) f |= M68K_C | M68K_X;
    ccr(c, 0, f);
    return r;
}
static inline uint32_t f_subx(M68k *c, uint32_t d, uint32_t s, int sz) {
    uint32_t m = M68K_MSB(sz), x = c->sr >> 4 & 1, r = (d - s - x) & M68K_MASK(sz);
    unsigned f = (r & m ? M68K_N : 0) | ((d ^ s) & (d ^ r) & m ? M68K_V : 0) | (r ? 0 : c->sr & M68K_Z);
    if (((s & ~d) | (r & ~d) | (s & r)) & m) f |= M68K_C | M68K_X;
    ccr(c, 0, f);
    return r;
}
/* BCD (the undocumented N and V as the chip computes them) */
static inline uint32_t f_abcd(M68k *c, uint32_t d, uint32_t s) {
    unsigned ss = s + d + (c->sr >> 4 & 1);
    unsigned bc = ((s & d) | (~ss & (s | d))) & 0x88;
    unsigned dc = (((ss + 0x66) ^ ss) & 0x110) >> 1;
    unsigned corf = (bc | dc) - ((bc | dc) >> 2);
    unsigned rr = ss + corf;
    unsigned f = ((bc | (ss & ~rr)) >> 7 & 1 ? M68K_C | M68K_X : 0) | ((~ss & rr) & 0x80 ? M68K_V : 0) |
                 (rr & 0x80 ? M68K_N : 0) | (rr & 0xFF ? 0 : c->sr & M68K_Z);
    ccr(c, 0, f);
    return rr & 0xFF;
}
static inline uint32_t f_sbcd(M68k *c, uint32_t d, uint32_t s) {
    unsigned dd = d - s - (c->sr >> 4 & 1);
    unsigned bc = ((~d & s) | (dd & ~d) | (dd & s)) & 0x88;
    unsigned corf = bc - (bc >> 2);
    unsigned rr = dd - corf;
    unsigned f = ((bc | (~dd & rr)) >> 7 & 1 ? M68K_C | M68K_X : 0) | ((dd & ~rr) & 0x80 ? M68K_V : 0) |
                 (rr & 0x80 ? M68K_N : 0) | (rr & 0xFF ? 0 : c->sr & M68K_Z);
    ccr(c, 0, f);
    return rr & 0xFF;
}

static inline int cond(const M68k *c, int cc) {
    unsigned s = c->sr;
    int C = s & 1, V = s >> 1 & 1, Z = s >> 2 & 1, N = s >> 3 & 1;
    switch (cc) {
    case 0: return 1;
    case 1: return 0;
    case 2: return !C && !Z;
    case 3: return C || Z;
    case 4: return !C;
    case 5: return C;
    case 6: return !Z;
    case 7: return Z;
    case 8: return !V;
    case 9: return V;
    case 10: return !N;
    case 11: return N;
    case 12: return N == V;
    case 13: return N != V;
    case 14: return !Z && N == V;
    default: return Z || N != V;
    }
}

/* shifts and rotates: kind 0 AS, 1 LS, 2 ROX, 3 RO; the result, flags set */
static inline uint32_t f_shift(M68k *c, int kind, int left, uint32_t v, unsigned n, int sz) {
    uint32_t m = M68K_MSB(sz), mask = M68K_MASK(sz);
    unsigned x = c->sr >> 4 & 1, carry = 0, vflag = 0;
    v &= mask;
    if (kind == 3 && n) {  /* ROL/ROR: C is the last bit out, X kept */
        unsigned bits = (unsigned)sz * 8, k = n % bits;
        if (k) v = (left ? v << k | v >> (bits - k) : v >> k | v << (bits - k)) & mask;
        carry = left ? v & 1 : (v & m) != 0;
        ccr(c, M68K_X, nz(v, sz) | (carry ? M68K_C : 0));
        return v;
    }
    for (unsigned i = 0; i < n; i++) {
        unsigned out = left ? (v & m) != 0 : v & 1;
        uint32_t old = v;
        if (left) v = (v << 1 | (kind == 2 ? x : 0)) & mask;
        else v = (v >> 1 | (kind == 0 ? old & m : kind == 2 && x ? m : 0)) & mask;
        if (kind == 0 && left && ((old ^ v) & m)) vflag = 1;
        carry = x = out;
    }
    unsigned f = nz(v, sz) | (vflag ? M68K_V : 0);
    if (n) f |= (carry ? M68K_C : 0) | (kind != 3 && x ? M68K_X : 0) | (kind == 3 ? c->sr & M68K_X : 0);
    else f |= (kind == 2 ? (x ? M68K_C : 0) : 0) | (c->sr & M68K_X);
    ccr(c, 0, f);
    return v;
}

/* ADDX.l/SUBX.l -(An): the low word, then the high one */
static inline uint32_t rd_pd32(M68k *c, int r) {
    uint32_t lo = rd16(c, c->a[r] - 2);
    uint32_t hi = rd16(c, c->a[r] - 4);
    c->a[r] -= 4;  /* once both reads went through */
    return hi << 16 | lo;
}

/* MOVEP: bytes at every other address, the high one first */
static inline void movep_load(M68k *c, uint32_t a, int r, int sz) {
    uint32_t v = 0;
    for (int i = 0; i < sz; i++) v = v << 8 | rd8(c, a + 2 * (uint32_t)i);
    setd(c, r, v, sz);
}
static inline void movep_store(M68k *c, uint32_t a, uint32_t v, int sz) {
    for (int i = sz - 1; i >= 0; i--, a += 2) wr8(c, a, v >> (8 * i));
}

/* MOVEM: registers D0-D7, A0-A7 by the mask's bits 0-15; -(An) takes them
 * in reverse (bit 0 is A7). Loads read one word past the last register. */
static inline void movem_reg(M68k *c, int i, uint32_t v) {
    if (i < 8) c->d[i] = v;
    else c->a[i - 8] = v;
}
static inline uint32_t movem_load(M68k *c, uint32_t mask, uint32_t a, int sz, int prog) {
    for (int i = 0; i < 16; i++) {
        if (!(mask >> i & 1)) continue;
        if (sz == 4) movem_reg(c, i, prog ? rdp32(c, a) : rd32(c, a));
        else movem_reg(c, i, sext(prog ? rdp16(c, a) : rd16(c, a), 2));
        a += (uint32_t)sz;
    }
    (void)(prog ? rdp16(c, a) : rd16(c, a));
    return a;
}
static inline void movem_store(M68k *c, uint32_t mask, uint32_t a, int sz) {
    for (int i = 0; i < 16; i++) {
        if (!(mask >> i & 1)) continue;
        uint32_t v = i < 8 ? c->d[i] : c->a[i - 8];
        if (sz == 4) wr32(c, a, v);
        else wr16(c, a, v);
        a += (uint32_t)sz;
    }
}
static inline void movem_pd(M68k *c, int r, uint32_t mask, int sz) {
    uint32_t a = c->a[r];
    for (int i = 0; i < 16; i++) {
        if (!(mask >> i & 1)) continue;
        uint32_t v = i < 8 ? c->a[7 - i] : c->d[15 - i];
        a -= (uint32_t)sz;
        if (sz == 4) wr32r(c, a, v);
        else wr16(c, a, v);
    }
    c->a[r] = a;
}

/* DIVU/DIVS into Dn: remainder in the high word, quotient in the low;
 * overflow leaves Dn and sets V */
static inline void div_u(M68k *c, int r, uint32_t s) {
    uint32_t q = c->d[r] / s, rem = c->d[r] % s;
    if (q > 0xFFFF) { ccr(c, M68K_X, M68K_V | M68K_N); return; }
    c->d[r] = rem << 16 | q;
    ccr(c, M68K_X, nz(q, 2));
}
static inline void div_s(M68k *c, int r, uint32_t s) {
    int64_t n = (int32_t)c->d[r], v = (int16_t)s, q = n / v, rem = n % v;
    if (q < -32768 || q > 32767) { ccr(c, M68K_X, M68K_V | M68K_N); return; }
    c->d[r] = (uint32_t)(rem & 0xFFFF) << 16 | (uint32_t)(q & 0xFFFF);
    ccr(c, M68K_X, nz((uint32_t)q, 2));
}

/* MULU/MULS: 38 + 2n clocks (n: ones of the source; MULS: 01/10 pairs) */
static inline unsigned mul_clocks(uint32_t s, int sign) {
    unsigned n = 0;
    s &= 0xFFFF;
    if (sign) s ^= s << 1;
    for (; s & 0xFFFF; s &= s - 1) n++;
    return 38 + 2 * n;
}
/* DIVU/DIVS clocks without the EA (Jorge Cwik, "68000 division timing") */
static inline unsigned divu_clocks(uint32_t dividend, uint16_t divisor) {
    if ((dividend >> 16) >= divisor) return 10;
    unsigned mc = 38;
    uint32_t hd = (uint32_t)divisor << 16;
    for (int i = 0; i < 15; i++) {
        uint32_t t = dividend;
        dividend <<= 1;
        if (t & 0x80000000u) dividend -= hd;
        else {
            mc += 2;
            if (dividend >= hd) dividend -= hd, mc--;
        }
    }
    return mc * 2;
}
static inline unsigned divs_clocks(int32_t dividend, int16_t divisor) {
    unsigned mc = dividend < 0 ? 7 : 6;
    uint32_t ad = dividend < 0 ? 0u - (uint32_t)dividend : (uint32_t)dividend;
    uint32_t av = divisor < 0 ? (uint32_t)-divisor : (uint32_t)divisor;
    if ((ad >> 16) >= av) return (mc + 2) * 2;
    uint32_t q = ad / av;
    mc += 55;
    if (divisor >= 0) mc += dividend >= 0 ? -1 : 1;
    for (int i = 0; i < 15; i++, q <<= 1)
        if (!(q & 0x8000)) mc++;
    return mc * 2;
}

#endif
