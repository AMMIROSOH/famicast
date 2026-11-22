/* C code for one 68000 instruction (core/recomp/template.h markers), from
 * the opcode's fields (Motorola M68000 PRM, the user's manual's timing
 * tables). Used for the interpreter's handlers (m68k_gen) and, later,
 * recompiled blocks, so the semantics exist once.
 *
 * Every bus access and idle span is in the order the CPU does it (checked
 * against SingleStepTests m68000, transaction by transaction): rd*, wr*,
 * idle() and the queue markers @W (an extension word: the queued word, the
 * next one read), @N (the last refill), @Q (the queued word, no access).
 * The lines read register numbers from `op` at run time, so one body serves
 * every register; recompiled code declares op as a constant. Helpers are
 * in m68k_ops.h. No line has two bus accesses in one expression. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "m68k.h"

static Lines *O;
#define L(...) lines_add(O, __VA_ARGS__)

enum { DN, AN, AI, PI, PD, DI, IX, AW, AL, PCD, PCI, IMM };
static int ea_of(int m, int r) { return m < 7 ? m : r <= 4 ? AW + r : -1; }
static int is_mem(int ea) { return ea >= AI && ea <= PCI; }
static int pcrel(int ea) { return ea == PCD || ea == PCI; }
static int alterable(int ea) { return ea >= 0 && ea <= AL; }        /* not PC-relative or immediate */
static int data_alt(int ea) { return ea >= 0 && ea != AN && ea <= AL; }
static int mem_alt(int ea) { return ea >= AI && ea <= AL; }
static int control(int ea) { return ea == AI || ea == DI || ea == IX || ea == AW || ea == AL || ea == PCD || ea == PCI; }

static const char *const RS = "(op & 7)", *const RD = "(op >> 9 & 7)";
static const char *const MASKS[5] = {"", " & 0xFF", " & 0xFFFF", "", ""};
static int SIZE[4] = {1, 2, 4, 0};  /* the usual size field (bits 7-6) */

/* (An)+ steps An once its access is done (a faulting one leaves it) */
static char post[64];
static void flush(void) {
    if (post[0]) L("%s", post), post[0] = 0;
}
/* The address of a memory operand into variable v. An address error on
 * the access after it pushes the PC as the microcode has it then: the
 * next prefetch address - 2, but - 4 after a displacement or index word
 * and + 0 after a byte or word -(An) (c->ae_adj). */
static void addr(int ea, const char *r, int sz, const char *v, int pd_idle) {
    switch (ea) {
    case AI: L("uint32_t %s = c->a[%s];", v, r); break;
    case PI:  /* a long's step waits for its access; a byte's or word's does not */
        if (sz < 4) { L("uint32_t %s = pi(c, %s, %d);", v, r, sz); break; }
        L("uint32_t %s = c->a[%s];", v, r);
        snprintf(post, sizeof post, "c->a[%s] += 4;", r);
        break;
    case PD:
        if (pd_idle) L("idle(c, 2);");
        L("uint32_t %s = pd(c, %s, %d);", v, r, sz);
        if (sz < 4) L("c->ae_adj = 0;");
        break;
    case DI: L("uint32_t %s = c->a[%s] + (uint32_t)(int16_t)@W;", v, r); break;
    case IX: L("idle(c, 2);"); L("uint32_t %s = ix(c, c->a[%s], @W);", v, r); break;
    case AW: L("uint32_t %s = (uint32_t)(int16_t)@W;", v); break;
    case AL: L("uint32_t %s = (uint32_t)@W << 16;", v); L("%s |= @W;", v); break;
    case PCD: L("uint32_t %s = @P - 2;", v); L("%s += (uint32_t)(int16_t)@W;", v); break;
    case PCI: L("idle(c, 2);"); L("uint32_t %s = @P - 2;", v); L("%s = ix(c, %s, @W);", v, v); break;
    default: fprintf(stderr, "m68k: addr of ea %d\n", ea); exit(2);
    }
    if (ea == DI || ea == IX || ea == PCD || ea == PCI) L("c->ae_adj = -4;");
}
static void imm(int sz, const char *v) {
    if (sz == 4) L("uint32_t %s = (uint32_t)@W << 16;", v), L("%s |= @W;", v);
    else L("uint32_t %s = @W%s;", v, MASKS[sz]);
}
/* the source operand's value into v (its address, if any, in v##a) */
static void src(int ea, const char *r, int sz, const char *v) {
    char a[8];
    if (ea == DN) { L("uint32_t %s = c->d[%s]%s;", v, r, MASKS[sz]); return; }
    if (ea == AN) { L("uint32_t %s = c->a[%s]%s;", v, r, MASKS[sz]); return; }
    if (ea == IMM) { imm(sz, v); return; }
    snprintf(a, sizeof a, "%sa", v);
    addr(ea, r, sz, a, 1);
    L("uint32_t %s = rd%s%d(c, %s);", v, pcrel(ea) ? "p" : "", sz * 8, a);
    flush();
}
/* read-modify-write: address in da, value in d; wb() writes r back */
static void rmw(int ea, const char *r, int sz) {
    addr(ea, r, sz, "da", 1);
    L("uint32_t d = rd%d(c, da);", sz * 8);
    flush();
}
static void wb(int sz, const char *v) {
    if (sz == 4) L("wr32r(c, da, %s);", v);
    else L("wr%d(c, da, %s);", sz * 8, v);
}

enum { ADD, SUB, AND, OR, EOR, CMP };
static const char *expr(int k, int sz, const char *d, const char *s) {
    static char e[96];
    switch (k) {
    case ADD: snprintf(e, sizeof e, "f_add(c, %s, %s, %d)", d, s, sz); break;
    case SUB: snprintf(e, sizeof e, "f_sub(c, %s, %s, %d, 0)", d, s, sz); break;
    case CMP: snprintf(e, sizeof e, "f_sub(c, %s, %s, %d, 1)", d, s, sz); break;
    case AND: snprintf(e, sizeof e, "f_logic(c, %s & %s, %d)", d, s, sz); break;
    case OR: snprintf(e, sizeof e, "f_logic(c, %s | %s, %d)", d, s, sz); break;
    default: snprintf(e, sizeof e, "f_logic(c, %s ^ %s, %d)", d, s, sz); break;
    }
    return e;
}

/* <ea>,Dn (ADD SUB AND OR CMP) */
static int alu_to_reg(int k, int ea, int sz) {
    if (ea < 0 || (ea == AN && (sz == 1 || k == AND || k == OR))) return 0;
    src(ea, RS, sz, "s");
    if (k == CMP) {
        L("(void)f_sub(c, c->d[%s]%s, s, %d, 1);", RD, MASKS[sz], sz);
    } else {
        L("uint32_t d = c->d[%s]%s;", RD, MASKS[sz]);
        L("setd(c, %s, %s, %d);", RD, expr(k, sz, "d", "s"), sz);
    }
    L("@N;");
    if (sz == 4) L("idle(c, %d);", k != CMP && (ea == DN || ea == AN || ea == IMM) ? 4 : 2);
    return 1;
}
/* Dn,<ea> (ADD SUB AND OR EOR): memory, or Dn for EOR */
static int alu_to_ea(int k, int ea, int sz) {
    if (k == EOR && ea == DN) {
        L("setd(c, %s, %s, %d);", RS, expr(k, sz, "(c->d[(op & 7)]", "c->d[(op >> 9 & 7)])"), sz);
        L("@N;");
        if (sz == 4) L("idle(c, 4);");
        return 1;
    }
    if (!mem_alt(ea)) return 0;
    rmw(ea, RS, sz);
    L("uint32_t r = %s;", expr(k, sz, "d", "c->d[(op >> 9 & 7)]"));
    L("@N;");
    wb(sz, "r");
    return 1;
}
/* ORI ANDI SUBI ADDI EORI CMPI #,<ea> */
static int alu_imm(int k, int ea, int sz) {
    if (!data_alt(ea)) return 0;
    imm(sz, "s");
    if (ea == DN) {
        if (k == CMP) L("(void)f_sub(c, c->d[%s]%s, s, %d, 1);", RS, MASKS[sz], sz);
        else L("uint32_t d = c->d[%s]%s;", RS, MASKS[sz]), L("setd(c, %s, %s, %d);", RS, expr(k, sz, "d", "s"), sz);
        L("@N;");
        if (sz == 4) L("idle(c, %d);", k == CMP ? 2 : 4);
        return 1;
    }
    rmw(ea, RS, sz);
    if (k == CMP) L("(void)%s;", expr(k, sz, "d", "s"));
    else L("uint32_t r = %s;", expr(k, sz, "d", "s"));
    L("@N;");
    if (k != CMP) wb(sz, "r");
    return 1;
}
/* ADDA SUBA CMPA <ea>,An */
static int alu_addr(int k, int ea, int sz) {
    if (ea < 0) return 0;
    src(ea, RS, sz, "s");
    L("s = sext(s, %d);", sz);
    if (k == CMP) L("(void)f_sub(c, c->a[%s], s, 4, 1);", RD);
    else L("c->a[%s] %s= s;", RD, k == ADD ? "+" : "-");
    L("@N;");
    L("idle(c, %d);", k == CMP ? 2 : sz == 2 || ea == DN || ea == AN || ea == IMM ? 4 : 2);
    return 1;
}
/* ADDQ SUBQ */
static int quick(int k, int ea, int sz) {
    if (!alterable(ea) || (ea == AN && sz == 1)) return 0;
    L("uint32_t s = (((op >> 9) - 1) & 7) + 1;");
    if (ea == AN) {
        L("c->a[%s] %s= s;", RS, k == ADD ? "+" : "-");
        L("@N;");
        L("idle(c, 4);");
    } else if (ea == DN) {
        L("uint32_t d = c->d[%s]%s;", RS, MASKS[sz]);
        L("setd(c, %s, %s, %d);", RS, expr(k, sz, "d", "s"), sz);
        L("@N;");
        if (sz == 4) L("idle(c, 4);");
    } else {
        rmw(ea, RS, sz);
        L("uint32_t r = %s;", expr(k, sz, "d", "s"));
        L("@N;");
        wb(sz, "r");
    }
    return 1;
}
/* NEGX CLR NEG NOT */
static int unary(int kind, int ea, int sz) {
    static const char *const E[4] = {"f_subx(c, 0, d, %d)", "f_logic(c, 0, %d)", "f_sub(c, 0, d, %d, 0)", "f_logic(c, ~d, %d)"};
    char e[48];
    if (!data_alt(ea)) return 0;
    snprintf(e, sizeof e, E[kind], sz);
    if (ea == DN) {
        L("uint32_t d = c->d[%s]%s;", RS, MASKS[sz]);
        if (kind == 1) L("(void)d;");
        L("setd(c, %s, %s, %d);", RS, e, sz);
        L("@N;");
        if (sz == 4) L("idle(c, 2);");
        return 1;
    }
    rmw(ea, RS, sz);
    if (kind == 1) L("(void)d;");
    L("uint32_t r = %s;", e);
    L("@N;");
    wb(sz, "r");
    return 1;
}

/* MOVE and MOVEA */
static int move(int op, int sz) {
    int se = ea_of(op >> 3 & 7, op & 7), de = ea_of(op >> 6 & 7, op >> 9 & 7);
    if (se < 0 || de < 0 || (se == AN && sz == 1) || de > AL || (de == AN && sz == 1)) return 0;
    src(se, RS, sz, "s");
    if (de == AN) {
        L("c->a[%s] = sext(s, %d);", RD, sz);
        L("@N;");
        return 1;
    }
    if (de == DN) {
        L("setd(c, %s, f_logic(c, s, %d), %d);", RD, sz, sz);
        L("@N;");
        return 1;
    }
    if (de == AL) {  /* with a memory source the write comes between the two address words */
        L("uint32_t da = (uint32_t)@W << 16;");
        if (is_mem(se)) L("da |= @Q;");
        else L("da |= @W;");
    } else if (de == PI) {  /* MOVE's destination steps once its write is done */
        L("uint32_t da = c->a[%s];", RD);
        snprintf(post, sizeof post, "c->a[%s] += inc_of(%s, %d);", RD, RD, sz);
    } else if (de == PD && sz == 4) {
        L("uint32_t da = c->a[%s] - 4;", RD);
        snprintf(post, sizeof post, "c->a[%s] = da;", RD);
    } else {
        addr(de, RD, sz, "da", 0);
    }
    /* a write fault pushes the prefetch address after (An) and (An)+, less 2 after the others */
    L("c->ae_adj = %d;", de == AI || de == PI ? 0 : -2);
    /* to (An) and (An)+ a long's flags are the low word's until its write is done */
    int early = sz == 4 && (de == AI || de == PI);
    L("(void)f_logic(c, s, %d);", early ? 2 : sz);
    if (de == PD) {
        L("@N;");
        L(sz == 4 ? "wr32r(c, da, s);" : "wr%d(c, da, s);", sz * 8);
        flush();
        return 1;
    }
    L(sz == 4 ? "wr32(c, da, s);" : "wr%d(c, da, s);", sz * 8);
    flush();
    if (early) L("(void)f_logic(c, s, 4);");
    if (de == AL && is_mem(se)) L("(void)@W;");
    L("@N;");
    return 1;
}

/* BTST BCHG BCLR BSET, the bit number in Dn (dyn) or an immediate word */
static int bitop(int kind, int ea, int dyn) {
    static const char *const OPS[4] = {"", "d ^ m", "d & ~m", "d | m"};
    if (ea < 0 || ea == AN || (kind && !data_alt(ea)) || (!dyn && ea == IMM)) return 0;
    if (dyn) L("uint32_t b = c->d[%s];", RD);
    else L("uint32_t b = @W;");
    if (ea == DN) {
        L("uint32_t d = c->d[%s], m = 1u << (b & 31);", RS);
        L("ccr(c, 0x1B, d & m ? 0 : M68K_Z);");
        if (kind) L("c->d[%s] = %s;", RS, OPS[kind]);
        L("@N;");
        if (kind == 0) L("idle(c, 2);");
        else if (kind == 2) L("idle(c, (b & 31) < 16 ? 4 : 6);");
        else L("idle(c, (b & 31) < 16 ? 2 : 4);");
        return 1;
    }
    if (ea == IMM) {  /* BTST Dn,#imm */
        L("uint32_t d = @W & 0xFF, m = 1u << (b & 7);");
        L("ccr(c, 0x1B, d & m ? 0 : M68K_Z);");
        L("@N;");
        L("idle(c, 2);");
        return 1;
    }
    char a[4] = "da";
    addr(ea, RS, 1, a, 1);
    L("uint32_t d = rd%s8(c, da), m = 1u << (b & 7);", pcrel(ea) ? "p" : "");
    flush();
    L("ccr(c, 0x1B, d & m ? 0 : M68K_Z);");
    L("@N;");
    if (kind) L("wr8(c, da, %s);", OPS[kind]);
    return 1;
}

static void exception(int vector, const char *pc) { L("@J m68k_exception(c, %d, %s, 4);", vector, pc); }
/* privileged: user mode raises the violation, the body runs inside else */
static void priv_open(void) {
    L("if (!(c->sr & M68K_S)) {");
    L("@J m68k_privilege(c);");
    L("} else {");
}
/* SR or CCR changed: the queue is read again from the instruction's end */
static void refetch(void) {
    L("c->ir = (uint16_t)bus_r(c, @P - 2, fcp(c), 3);");
    L("c->irc = (uint16_t)bus_r(c, @P, fcp(c), 3);");
    L("@J @P + 2;");
}

static int line0(uint16_t op) {
    int ea = ea_of(op >> 3 & 7, op & 7), sz = SIZE[op >> 6 & 3];
    if (op & 0x100) {
        if ((op >> 3 & 7) == 1) {  /* MOVEP */
            int w = op >> 7 & 1, l = op >> 6 & 1;
            L("uint32_t a = c->a[%s] + (uint32_t)(int16_t)@W;", RS);
            L(w ? "movep_store(c, a, c->d[%s], %d);" : "movep_load(c, a, %s, %d);", RD, l ? 4 : 2);
            L("@N;");
            return 1;
        }
        return bitop(op >> 6 & 3, ea, 1);
    }
    int t = op >> 9 & 7;
    if (t == 4) return (op & 0xFF00) == 0x0800 ? bitop(op >> 6 & 3, ea, 0) : 0;
    if (t == 7 || sz == 0) return 0;
    if ((op & 0x3F) == 0x3C && (t == 0 || t == 1 || t == 5) && sz < 4) {  /* to CCR / SR */
        static const char *const OPN[] = {"|", "&", "", "", "", "^"};
        int sr = sz == 2;
        if (sr) priv_open();
        L("uint32_t s = @W;");
        L("idle(c, 8);");
        if (sr) L("m68k_set_sr(c, (uint16_t)(c->sr %s s));", OPN[t]);
        else L("c->sr = (uint16_t)((c->sr & 0xFF00) | ((c->sr %s s) & 0x1F));", OPN[t]);
        refetch();
        if (sr) L("}");
        return 1;
    }
    static const int K[8] = {OR, AND, SUB, ADD, -1, EOR, CMP, -1};
    return alu_imm(K[t], ea, sz);
}

static int movem(uint16_t op) {
    int ea = ea_of(op >> 3 & 7, op & 7), sz = op & 0x40 ? 4 : 2, load = op & 0x400;
    if (load ? !(control(ea) || ea == PI) : !(mem_alt(ea) && ea != PI)) return 0;
    L("uint32_t mask = @W;");
    if (ea == PD) L("c->ae_adj = 0;"), L("movem_pd(c, %s, mask, %d);", RS, sz);
    else if (ea == PI) L("c->ae_adj = 0;"), L("c->a[%s] = movem_load(c, mask, c->a[%s], %d, 0);", RS, RS, sz);
    else {
        addr(ea, RS, sz, "a", 0);
        L("c->ae_adj = 0;");  /* MOVEM pushes the prefetch address itself */
        if (load) L("(void)movem_load(c, mask, a, %d, %d);", sz, pcrel(ea));
        else L("movem_store(c, mask, a, %d);", sz);
    }
    L("@N;");
    return 1;
}

static int line4(uint16_t op) {
    int ea = ea_of(op >> 3 & 7, op & 7), sz = SIZE[op >> 6 & 3], r = op >> 3 & 7;
    if (op & 0x100) {
        if ((op & 0x1C0) == 0x1C0) {  /* LEA */
            if (!control(ea)) return 0;
            if (ea == IX || ea == PCI) {
                L("idle(c, 2);");
                if (ea == IX) L("uint32_t a = ix(c, c->a[%s], @W);", RS);
                else L("uint32_t a = @P - 2;"), L("a = ix(c, a, @W);");
                L("idle(c, 2);");
            } else {
                addr(ea, RS, 4, "a", 0);
            }
            L("c->a[%s] = a;", RD);
            L("@N;");
            return 1;
        }
        if ((op & 0x1C0) == 0x180) {  /* CHK.w */
            if (ea < 0 || ea == AN) return 0;
            src(ea, RS, 2, "s");
            L("int16_t d = (int16_t)c->d[%s];", RD);
            L("if (d < 0 || d > (int16_t)s) {");
            L("ccr(c, M68K_X, d < 0 ? M68K_N : 0);");
            /* the microcode's 16-bit s - d: below zero (or d >= 0) traps after 8 clocks, else 10 */
            L("@J m68k_exception(c, 6, @P - 2, d < 0 && !((s - (uint32_t)d) & 0x8000) ? 10 : 8);");
            L("} else {");
            L("ccr(c, M68K_X, 0);");
            L("idle(c, 6);");
            L("@N;");
            L("}");
            return 1;
        }
        return 0;
    }
    switch (op >> 8 & 0xF) {
    case 0x0:
        if (sz) return unary(0, ea, sz);
        if (!data_alt(ea)) return 0;  /* MOVE from SR */
        if (ea == DN) { L("setd(c, %s, c->sr, 2);", RS); L("@N;"); L("idle(c, 2);"); return 1; }
        rmw(ea, RS, 2);
        L("(void)d;");
        L("@N;");
        L("wr16(c, da, c->sr);");
        return 1;
    case 0x2: return sz ? unary(1, ea, sz) : 0;
    case 0x4:
    case 0x6:
        if (sz) return unary((op >> 8 & 0xF) == 4 ? 2 : 3, ea, sz);
        if (ea < 0 || ea == AN) return 0;  /* MOVE to CCR / SR */
        if (op & 0x200) priv_open();
        src(ea, RS, 2, "s");
        L("idle(c, 4);");
        if (op & 0x200) L("m68k_set_sr(c, (uint16_t)s);");
        else L("c->sr = (uint16_t)((c->sr & 0xFF00) | (s & 0x1F));");
        refetch();
        if (op & 0x200) L("}");
        return 1;
    case 0x8:
        if ((op & 0xC0) == 0) {  /* NBCD */
            if (!data_alt(ea)) return 0;
            if (ea == DN) {
                L("setd(c, %s, f_sbcd(c, 0, c->d[%s] & 0xFF), 1);", RS, RS);
                L("@N;");
                L("idle(c, 2);");
                return 1;
            }
            rmw(ea, RS, 1);
            L("uint32_t r = f_sbcd(c, 0, d);");
            L("@N;");
            wb(1, "r");
            return 1;
        }
        if ((op & 0xC0) == 0x40) {
            if (r == 0) { L("c->d[%s] = f_logic(c, c->d[%s] >> 16 | c->d[%s] << 16, 4);", RS, RS, RS); L("@N;"); return 1; }
            if (!control(ea)) return 0;  /* PEA */
            if (ea == IX || ea == PCI) {
                L("idle(c, 2);");
                if (ea == IX) L("uint32_t a = ix(c, c->a[%s], @W);", RS);
                else L("uint32_t a = @P - 2;"), L("a = ix(c, a, @W);");
                L("idle(c, 2);");
            } else {
                addr(ea, RS, 4, "a", 0);
            }
            if (ea == AW || ea == AL) L("c->a[7] -= 4;"), L("wr32(c, c->a[7], a);"), L("@N;");
            else L("@N;"), L("c->a[7] -= 4;"), L("wr32(c, c->a[7], a);");
            return 1;
        }
        if (r == 0) {  /* EXT */
            if (op & 0x40) L("c->d[%s] = f_logic(c, sext(c->d[%s], 2), 4);", RS, RS);
            else L("setd(c, %s, f_logic(c, sext(c->d[%s], 1), 2), 2);", RS, RS);
            L("@N;");
            return 1;
        }
        return movem(op);
    case 0xA:
        if (op == 0x4AFC) return 0;  /* ILLEGAL */
        if (sz) {
            if (ea < 0 || (ea == AN && sz == 1) || ea == IMM || pcrel(ea) || ea == AN) return 0;  /* TST */
            src(ea, RS, sz, "s");
            L("(void)f_logic(c, s, %d);", sz);
            L("@N;");
            return 1;
        }
        if (!data_alt(ea)) return 0;  /* TAS */
        if (ea == DN) {
            L("uint32_t d = c->d[%s] & 0xFF;", RS);
            L("(void)f_logic(c, d, 1);");
            L("setd(c, %s, d | 0x80, 1);", RS);
            L("@N;");
            return 1;
        }
        rmw(ea, RS, 1);
        L("(void)f_logic(c, d, 1);");
        L("idle(c, 2);");
        L("wr8(c, da, d | 0x80);");
        L("@N;");
        return 1;
    case 0xC:
        if (op & 0x80) return movem(op);
        return 0;
    case 0xE:
        if ((op & 0xC0) == 0x80 || (op & 0xC0) == 0xC0) {  /* JSR JMP */
            int jsr = !(op & 0x40);
            if (!control(ea)) return 0;
            int ext = ea == AL ? 2 : (ea == AI ? 0 : 1);
            if (ea == AL) L("uint32_t t = (uint32_t)@W << 16;"), L("t |= @Q;");
            else {
                if (ea != AI) L("idle(c, %d);", ea == IX || ea == PCI ? 6 : 2);
                switch (ea) {
                case AI: L("uint32_t t = c->a[%s];", RS); break;
                case DI: L("uint32_t t = c->a[%s] + (uint32_t)(int16_t)@Q;", RS); break;
                case IX: L("uint32_t t = ix(c, c->a[%s], @Q);", RS); break;
                case AW: L("uint32_t t = (uint32_t)(int16_t)@Q;"); break;
                case PCD: L("uint32_t t = @P - 2 + (uint32_t)(int16_t)@Q;"); break;
                default: L("uint32_t t = ix(c, @P - 2, @Q);"); break;
                }
            }
            if (!jsr) {
                if (ea == AL) L("c->ae_adj = -4;");
                L("@J jump_to(c, t);");
                return 1;
            }
            L("uint32_t ret = @P + %d;", ext == 0 ? -2 : ext == 1 ? 0 : 0);
            if (ext) L("c->ae_adj = 0;");
            L("@J jump_first(c, t);");
            L("c->a[7] -= 4;");
            L("wr32(c, c->a[7], ret);");
            L("jump_rest(c);");
            return 1;
        }
        if ((op & 0xC0) != 0x40) return 0;
        switch (op >> 3 & 7) {
        case 0: case 1: exception(32 + (op & 15), "@P - 2"); return 1;  /* TRAP */
        case 2:  /* LINK */
            L("uint32_t d = (uint32_t)(int16_t)@W, v = c->a[%s];", RS);  /* LINK A7 pushes A7 as it was */
            L("c->a[7] -= 4;");
            L("wr32(c, c->a[7], v);");
            L("c->a[%s] = c->a[7];", RS);
            L("c->a[7] += d;");
            L("@N;");
            return 1;
        case 3:  /* UNLK */
            L("c->ae_adj = 0;");
            L("uint32_t v = rd32(c, c->a[%s]);", RS);
            L("c->a[7] = c->a[%s] + 4;", RS);
            L("c->a[%s] = v;", RS);
            L("@N;");
            return 1;
        case 4: case 5:  /* MOVE USP */
            priv_open();
            if (op & 8) L("c->a[%s] = c->usp;", RS);
            else L("c->usp = c->a[%s];", RS);
            L("@N;");
            L("}");
            return 1;
        case 6:
            switch (op & 7) {
            case 0: priv_open(); L("m68k_reset_out(c);"); L("idle(c, 128);"); L("@N;"); L("}"); return 1;  /* RESET */
            case 1: L("@N;"); return 1;  /* NOP */
            case 2:  /* STOP */
                priv_open();
                L("m68k_set_sr(c, (uint16_t)@Q);");  /* the queue stays: the PC is not stepped */
                L("idle(c, 4);");
                L("c->stopped = 1;");
                L("}");
                return 1;
            case 3:  /* RTE */
                priv_open();
                L("uint32_t s = rd16(c, c->a[7]);");
                L("uint32_t t = rd32(c, c->a[7] + 2);");
                L("c->a[7] += 6;");
                L("m68k_set_sr(c, (uint16_t)s);");
                L("@J jump_to(c, t);");
                L("}");
                return 1;
            case 5:  /* RTS */
                L("uint32_t t = rd32(c, c->a[7]);");
                L("c->a[7] += 4;");
                L("@J jump_to(c, t);");
                return 1;
            case 6:  /* TRAPV */
                L("if (c->sr & M68K_V) {");
                L("@N;");
                L("@J m68k_exception(c, 7, @P - 4, 0);");
                L("} else {");
                L("@N;");
                L("}");
                return 1;
            case 7:  /* RTR */
                L("uint32_t s = rd16(c, c->a[7]);");
                L("uint32_t t = rd32(c, c->a[7] + 2);");
                L("c->a[7] += 6;");
                L("c->sr = (uint16_t)((c->sr & 0xFF00) | (s & 0x1F));");
                L("@J jump_to(c, t);");
                return 1;
            }
            return 0;
        }
        return 0;
    }
    return 0;
}

static int line5(uint16_t op) {
    int ea = ea_of(op >> 3 & 7, op & 7), cc = op >> 8 & 15;
    if ((op & 0xC0) != 0xC0) return quick(op & 0x100 ? SUB : ADD, ea, SIZE[op >> 6 & 3]);
    if ((op >> 3 & 7) == 1) {  /* DBcc */
        L("c->ae_adj = 0;");
        L("if (cond(c, %d)) {", cc);
        L("idle(c, 4);");
        L("(void)@W;");
        L("@N;");
        L("} else if ((uint16_t)(c->d[%s] - 1) != 0xFFFF) {", RS);
        L("idle(c, 2);");
        L("@B jump_to(c, @P - 2 + (uint32_t)(int16_t)@Q);");
        L("dbcc_dec(c, %s);", RS);  /* only once the branch's fetch went through */
        L("} else {");
        L("dbcc_dec(c, %s);", RS);
        L("idle(c, 2);");
        L("(void)@W;");
        L("(void)rdp16(c, @P);");
        L("@N;");
        L("}");
        return 1;
    }
    if (!data_alt(ea)) return 0;  /* Scc */
    L("uint32_t v = cond(c, %d) ? 0xFF : 0;", cc);
    if (ea == DN) {
        L("setd(c, %s, v, 1);", RS);
        L("@N;");
        L("if (v) idle(c, 2);");
        return 1;
    }
    rmw(ea, RS, 1);
    L("(void)d;");
    L("@N;");
    L("wr8(c, da, v);");
    return 1;
}

static int line6(uint16_t op) {
    int cc = op >> 8 & 15, w = (op & 0xFF) == 0;
    const char *disp = w ? "(uint32_t)(int16_t)@Q" : "(uint32_t)(int8_t)op";
    if (cc == 1) {  /* BSR */
        L("uint32_t t = @P - 2 + %s;", disp);
        L("uint32_t ret = @P%s;", w ? "" : " - 2");
        L("idle(c, 2);");
        L("c->a[7] -= 4;");
        L("wr32(c, c->a[7], ret);");
        L("c->ae_adj = 0;");  /* an odd target pushes the target */
        L("@J t;");
        L("@J jump_to(c, t);");
        return 1;
    }
    L("if (cond(c, %d)) {", cc);
    L("idle(c, 2);");
    L("@B jump_to(c, @P - 2 + %s);", disp);
    L("} else {");
    L("idle(c, 4);");
    if (w) L("(void)@W;");
    L("@N;");
    L("}");
    return 1;
}

/* ABCD SBCD ADDX SUBX: Dy,Dx or -(Ay),-(Ax) */
static int extended(int kind, uint16_t op, int sz) {
    static const char *const F[4] = {"f_abcd(c, d, s)", "f_sbcd(c, d, s)", "f_addx(c, d, s, %d)", "f_subx(c, d, s, %d)"};
    char e[40];
    snprintf(e, sizeof e, F[kind], sz);
    if (!(op & 8)) {
        L("uint32_t s = c->d[%s]%s, d = c->d[%s]%s;", RS, MASKS[sz], RD, MASKS[sz]);
        L("setd(c, %s, %s, %d);", RD, e, sz);
        L("@N;");
        if (kind < 2) L("idle(c, 2);");
        else if (sz == 4) L("idle(c, 4);");
        return 1;
    }
    L("c->ae_adj = 0;");
    L("idle(c, 2);");
    if (sz == 4) {
        L("uint32_t s = rd_pd32(c, %s);", RS);
        L("uint32_t d = rd_pd32(c, %s);", RD);
        L("uint32_t r = %s;", e);
        L("wr16(c, c->a[%s] + 2, r);", RD);
        L("@N;");
        L("wr16(c, c->a[%s], r >> 16);", RD);
        return 1;
    }
    L("uint32_t s = rd%d(c, pd(c, %s, %d));", sz * 8, RS, sz);
    L("uint32_t da = pd(c, %s, %d);", RD, sz);
    L("uint32_t d = rd%d(c, da);", sz * 8);
    L("uint32_t r = %s;", e);
    L("@N;");
    L("wr%d(c, da, r);", sz * 8);
    return 1;
}

/* lines 8 9 B C D: OR SUB CMP/EOR AND ADD and their neighbours */
static int arith(uint16_t op) {
    int line = op >> 12, opm = op >> 6 & 7, ea = ea_of(op >> 3 & 7, op & 7), sz = SIZE[opm & 3];
    int k = line == 8 ? OR : line == 9 ? SUB : line == 0xB ? CMP : line == 0xC ? AND : ADD;
    if (opm == 3 || opm == 7) {
        int wsz = opm == 3 ? 2 : 4;
        if (line == 9 || line == 0xB || line == 0xD) return alu_addr(k, ea, wsz);
        if (ea < 0 || ea == AN) return 0;
        src(ea, RS, 2, "s");  /* MULU MULS DIVU DIVS */
        if (line == 0xC) {
            int sign = opm == 7;
            L(sign ? "uint32_t r = (uint32_t)((int32_t)(int16_t)s * (int16_t)c->d[%s]);" : "uint32_t r = s * (c->d[%s] & 0xFFFF);", RD);
            L("c->d[%s] = f_logic(c, r, 4);", RD);
            L("@N;");
            L("idle(c, mul_clocks(s, %d) - 4);", sign);
            return 1;
        }
        L("c->ae_adj = -2;");
        L("if (!s) {");
        L("idle(c, 4);");
        L("@J m68k_exception(c, 5, @P - 2, 4);");
        L("} else {");
        L(opm == 3 ? "idle(c, divu_clocks(c->d[%s], (uint16_t)s) - 4);" : "idle(c, divs_clocks((int32_t)c->d[%s], (int16_t)s) - 4);", RD);
        L(opm == 3 ? "div_u(c, %s, s);" : "div_s(c, %s, s);", RD);
        L("@N;");
        L("}");
        return 1;
    }
    if (opm < 3) {
        return alu_to_reg(k, ea, sz);
    }
    /* opm 4-6: Dn,<ea>, or the register/memory forms with mode 0 or 1 */
    int mode = op >> 3 & 7;
    if (mode <= 1) {
        if (line == 8 || line == 0xC) {
            if (opm == 4) return extended(line == 8 ? 1 : 0, op, 1);
            if (line == 0xC && opm == 5) {  /* EXG Dx,Dy / Ax,Ay */
                const char *t = mode ? "a" : "d";
                L("uint32_t t = c->%s[%s];", t, RD);
                L("c->%s[%s] = c->%s[%s];", t, RD, t, RS);
                L("c->%s[%s] = t;", t, RS);
                L("@N;");
                L("idle(c, 2);");
                return 1;
            }
            if (line == 0xC && opm == 6 && mode == 1) {  /* EXG Dx,Ay */
                L("uint32_t t = c->d[%s];", RD);
                L("c->d[%s] = c->a[%s];", RD, RS);
                L("c->a[%s] = t;", RS);
                L("@N;");
                L("idle(c, 2);");
                return 1;
            }
            return 0;
        }
        if (line == 9 || line == 0xD) return extended(line == 9 ? 3 : 2, op, sz);
        if (mode == 1) {  /* CMPM (Ay)+,(Ax)+ */
            L("c->ae_adj = 0;");
            if (sz == 4) {  /* (Ay)+ steps by 2 before its first read, the rest after; (Ax)+ after */
                L("uint32_t sa = c->a[%s];", RS);
                L("c->a[%s] += 2;", RS);
                L("uint32_t s = rd32(c, sa);");
                L("c->a[%s] += 2;", RS);
                L("uint32_t d = rd32(c, c->a[%s]);", RD);
                L("c->a[%s] += 4;", RD);
            } else {
                L("uint32_t s = rd%d(c, pi(c, %s, %d));", sz * 8, RS, sz);
                L("uint32_t d = rd%d(c, c->a[%s]);", sz * 8, RD);
                L("c->a[%s] += inc_of(%s, %d);", RD, RD, sz);
            }
            L("(void)f_sub(c, d, s, %d, 1);", sz);
            L("@N;");
            return 1;
        }
        return alu_to_ea(EOR, ea, sz);
    }
    return alu_to_ea(line == 0xB ? EOR : k, ea, sz);
}

static int lineE(uint16_t op) {
    int ty, left = op >> 8 & 1;
    if ((op & 0xC0) == 0xC0) {  /* memory, by one */
        int ea = ea_of(op >> 3 & 7, op & 7);
        if ((op & 0x800) || !mem_alt(ea)) return 0;
        ty = op >> 9 & 3;
        rmw(ea, RS, 2);
        L("uint32_t r = f_shift(c, %d, %d, d, 1, 2);", ty, left);
        L("@N;");
        L("wr16(c, da, r);");
        return 1;
    }
    int sz = SIZE[op >> 6 & 3];
    ty = op >> 3 & 3;
    if (op & 0x20) L("unsigned n = c->d[%s] & 63;", RD);
    else L("unsigned n = (((op >> 9) - 1) & 7) + 1;");
    L("setd(c, %s, f_shift(c, %d, %d, c->d[%s], n, %d), %d);", RS, ty, left, RS, sz, sz);
    L("@N;");
    L("idle(c, %d + 2 * n);", sz == 4 ? 4 : 2);
    return 1;
}

int m68k_body(uint16_t op, Lines *out) {
    out->n = 0;
    O = out;
    post[0] = 0;
    int ok = 0;
    switch (op >> 12) {
    case 0x0: ok = line0(op); break;
    case 0x1: ok = move(op, 1); break;
    case 0x2: ok = move(op, 4); break;
    case 0x3: ok = move(op, 2); break;
    case 0x4: ok = line4(op); break;
    case 0x5: ok = line5(op); break;
    case 0x6: ok = line6(op); break;
    case 0x7:
        if (op & 0x100) break;
        L("c->d[%s] = f_logic(c, (uint32_t)(int8_t)op, 4);", RD);
        L("@N;");
        ok = 1;
        break;
    case 0xA: exception(10, "@P - 4"); return 1;
    case 0xF: exception(11, "@P - 4"); return 1;
    case 0xE: ok = lineE(op); break;
    default: ok = arith(op); break;
    }
    if (!ok) {
        out->n = 0;
        exception(4, "@P - 4");
    }
    return ok;
}

/* ---- instruction lengths ---- */
static int eaw(int ea, int sz) {
    switch (ea) {
    case DI: case IX: case AW: case PCD: case PCI: return 1;
    case AL: return 2;
    case IMM: return sz == 4 ? 2 : 1;
    default: return 0;
    }
}

int m68k_ext_words(uint16_t op) {
    int ea = ea_of(op >> 3 & 7, op & 7), sz = SIZE[op >> 6 & 3], mode = op >> 3 & 7;
    switch (op >> 12) {
    case 0x0:
        if (op & 0x100) return mode == 1 ? 1 : eaw(ea, 1);   /* MOVEP; BTST/BCHG/BCLR/BSET Dn */
        if ((op >> 9 & 7) == 4) return 1 + eaw(ea, 1);         /* BTST/BCHG/BCLR/BSET #n */
        if ((op & 0x3F) == 0x3C) return 1;                      /* ORI/ANDI/EORI to CCR or SR */
        return (sz == 4 ? 2 : 1) + eaw(ea, sz);                 /* ORI ANDI SUBI ADDI EORI CMPI */
    case 0x1: case 0x2: case 0x3: {
        int s = op >> 12 == 1 ? 1 : op >> 12 == 2 ? 4 : 2;
        return eaw(ea, s) + eaw(ea_of(op >> 6 & 7, op >> 9 & 7), s);
    }
    case 0x4:
        if (op == 0x4AFC) return 0;                             /* ILLEGAL */
        if ((op & 0xFFC0) == 0x40C0 || (op & 0xFFC0) == 0x44C0 || (op & 0xFFC0) == 0x46C0) return eaw(ea, 2);  /* MOVE SR, CCR */
        if ((op & 0xFB80) == 0x4880) return mode == 0 ? 0 : 1 + eaw(ea, 2);  /* EXT; MOVEM */
        if ((op & 0xFFF8) == 0x4E50 || op == 0x4E72) return 1;  /* LINK, STOP */
        if ((op & 0xFF80) == 0x4E80) return eaw(ea, 4);         /* JSR, JMP */
        if ((op & 0xF1C0) == 0x41C0) return eaw(ea, 4);         /* LEA */
        if ((op & 0xF1C0) == 0x4180) return eaw(ea, 2);         /* CHK */
        if ((op & 0xFFC0) == 0x4840) return mode == 0 ? 0 : eaw(ea, 4);  /* SWAP, PEA */
        if ((op & 0xFFC0) == 0x4E40) return 0;                  /* TRAP LINK UNLK MOVE USP RESET NOP RTE RTS TRAPV RTR */
        if ((op & 0xFFC0) == 0x4800 || (op & 0xFFC0) == 0x4AC0) return eaw(ea, 1);  /* NBCD, TAS */
        return eaw(ea, sz);                                     /* NEGX CLR NEG NOT TST */
    case 0x5: return (op & 0xC0) == 0xC0 ? (mode == 1 ? 1 : eaw(ea, 1)) : eaw(ea, sz);  /* DBcc, Scc; ADDQ SUBQ */
    case 0x6: return (op & 0xFF) == 0;                          /* Bcc BRA BSR: a word displacement */
    case 0x8:
        if ((op & 0xC0) == 0xC0) return eaw(ea, 2);             /* DIVU DIVS */
        return (op & 0x1F0) == 0x100 ? 0 : eaw(ea, sz);         /* SBCD; OR */
    case 0xC:
        if ((op & 0xC0) == 0xC0) return eaw(ea, 2);             /* MULU MULS */
        if ((op & 0x1F0) == 0x100 || (op & 0x1F8) == 0x140 || (op & 0x1F8) == 0x148 || (op & 0x1F8) == 0x188) return 0;  /* ABCD EXG */
        return eaw(ea, sz);                                     /* AND */
    case 0x9: case 0xD:
        if ((op & 0xC0) == 0xC0) return eaw(ea, op & 0x100 ? 4 : 2);  /* SUBA ADDA */
        return (op & 0x130) == 0x100 ? 0 : eaw(ea, sz);         /* SUBX ADDX; SUB ADD */
    case 0xB:
        if ((op & 0xC0) == 0xC0) return eaw(ea, op & 0x100 ? 4 : 2);  /* CMPA */
        return (op & 0x138) == 0x108 ? 0 : eaw(ea, sz);         /* CMPM; CMP EOR */
    case 0xE: return (op & 0xC0) == 0xC0 ? eaw(ea, 2) : 0;      /* memory shifts; register shifts */
    default: return 0;                                          /* MOVEQ; line A and F */
    }
}

/* ---- the interpreter's handlers ---- */
static unsigned long hash(const char *s) {
    unsigned long h = 5381;
    while (*s) h = h * 33 + (unsigned char)*s++;
    return h;
}

void m68k_cpu_handlers(Buf *o) {
    enum { SLOTS = 1 << 14 };
    static char *bodies[SLOTS];
    static int slot_of[SLOTS];
    static uint16_t index[65536];
    int n = 0;
    for (int i = 0; i < SLOTS; i++) slot_of[i] = -1;
    char **text = calloc(65536, sizeof *text);
    int bad = 0;
    for (int op = 0; op < 65536; op++) {
        Lines l;
        int legal = m68k_body((uint16_t)op, &l);
        /* the length the recompiler uses must match the words the template fetches (paths without branches) */
        int words = 0, branchy = 0;
        for (int i = 0; i < l.n; i++) {
            for (const char *s = l.s[i]; (s = strchr(s, '@')); s++) words += s[1] == 'W', branchy |= s[1] == 'Q' || s[1] == 'J' || s[1] == 'B';
            branchy |= !strncmp(l.s[i], "if (", 4) || !strncmp(l.s[i], "} else", 6);
        }
        if (legal && !branchy && words != m68k_ext_words((uint16_t)op) && bad++ < 10)
            fprintf(stderr, "m68k: $%04X fetches %d extension words, m68k_ext_words says %d\n", op, words, m68k_ext_words((uint16_t)op));
        Buf b = {0};
        for (int i = 0; i < l.n; i++) template_plain(&b, "    ", l.s[i]);
        unsigned long h = hash(b.s) % SLOTS;
        while (slot_of[h] >= 0 && strcmp(bodies[slot_of[h]], b.s)) h = (h + 1) % SLOTS;
        if (slot_of[h] < 0) {
            if (n == SLOTS) { fprintf(stderr, "m68k: too many handlers\n"); exit(2); }
            bodies[n] = b.s;
            slot_of[h] = n++;
        } else {
            free(b.s);
        }
        index[op] = (uint16_t)slot_of[h];
    }
    free(text);
    if (bad) { fprintf(stderr, "m68k: %d opcodes with a wrong length\n", bad); exit(2); }
    bprintf(o, "/* generated by m68k_gen from cpu/m68k/m68k.c; do not edit */\n");
    for (int i = 0; i < n; i++)
        bprintf(o, "static void m68k_h%d(M68k *c, uint16_t op) {\n    (void)op;\n%s}\n", i, bodies[i]);
    bprintf(o, "static void (*const m68k_handlers[%d])(M68k *, uint16_t) = {\n", n);
    for (int i = 0; i < n; i++) bprintf(o, "    m68k_h%d,\n", i);
    bprintf(o, "};\nstatic const uint16_t m68k_index[65536] = {\n");
    for (int op = 0; op < 65536; op++) bprintf(o, "%d,%s", index[op], op % 16 == 15 ? "\n" : "");
    bprintf(o, "};\n");
}
