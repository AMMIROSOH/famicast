/* C code for one Z80 instruction (core/recomp/template.h markers), from the
 * opcode's bit fields (Zilog's Z80 manual, Sean Young "The Undocumented Z80
 * Documented", the decoding of opcodes by x, y, z, p, q). Used for the
 * interpreter's switch (z80_gen) and, later, recompiled blocks, so the
 * semantics exist once.
 *
 * The DD and FD tables are not written again: the base table's code is made
 * with an index register (idx 1 = IX, 2 = IY) that replaces HL, H and L, and
 * (HL) becomes (IX+d). An opcode that never touched one of them has no line
 * of its own in DD and FD: z80_body returns 0 and the switch runs the base
 * opcode (prefix and opcode both fetched, as the hardware does).
 *
 * Every rd()/z80_wr()/z80_tick() is one bus cycle or internal cycle in the
 * order the CPU does them (checked T-state by T-state against
 * SingleStepTests z80). The first opcode byte (M1) is fetched by the caller;
 * a prefix's next byte by the prefix's case. Helpers live in z80_ops.h.
 * C leaves the order of a call's arguments open: no line has two bus
 * accesses. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "z80.h"

static const char *const R8[8] = {"c->b", "c->c", "c->d", "c->e", "c->h", "c->l", "", "c->a"};
static const char *const NAME8[3][8] = {{"B", "C", "D", "E", "H", "L", "(HL)", "A"},
                                        {"B", "C", "D", "E", "IXH", "IXL", "(IX+d)", "A"},
                                        {"B", "C", "D", "E", "IYH", "IYL", "(IY+d)", "A"}};
static const char *const IDXH[3] = {"c->h", "c->ixh", "c->iyh"}, *const IDXL[3] = {"c->l", "c->ixl", "c->iyl"};
static const char *const IDXN[3] = {"HL", "IX", "IY"};
static const char *const IDXGET[3] = {"z80_hl(c)", "z80_ix(c)", "z80_iy(c)"};
static const char *const IDXSET[3] = {"z80_set_hl", "z80_set_ix", "z80_set_iy"};
static const char *const ALU[8] = {"ADD", "ADC", "SUB", "SBC", "AND", "XOR", "OR", "CP"};
static const char *const SHIFT[8] = {"RLC", "RRC", "RL", "RR", "SLA", "SRA", "SLL", "SRL"};
static const char *const COND[8] = {"!(c->f & Z80_FLAG_Z)", "(c->f & Z80_FLAG_Z)", "!(c->f & Z80_FLAG_C)", "(c->f & Z80_FLAG_C)",
                                    "!(c->f & Z80_FLAG_PV)", "(c->f & Z80_FLAG_PV)", "!(c->f & Z80_FLAG_S)", "(c->f & Z80_FLAG_S)"};
static const char *const CONDN[8] = {"NZ", "Z", "NC", "C", "PO", "PE", "P", "M"};
/* rr for LD/INC/DEC/ADD/ED (BC DE HL SP) and PUSH/POP (BC DE HL AF) */
static const char *const PAIRN[4] = {"BC", "DE", "HL", "SP"};
static const char *const PAIRGET[4] = {"z80_bc(c)", "z80_de(c)", "", "c->sp"};
static const char *const PAIRSET[4] = {"z80_set_bc", "z80_set_de", "", ""};
static const char *const HI[4] = {"c->b", "c->d", "c->h", "c->a"}, *const LO[4] = {"c->c", "c->e", "c->l", "c->f"};

static int used;  /* the opcode being made touched HL, H, L or (HL), so IX and IY change it */

/* register r (not 6) of the opcode; H and L are IXH and IXL unless `real` (the other operand is (IX+d)) */
static const char *reg(int r, int idx, int real) {
    if (idx && !real && (r == 4 || r == 5)) return used = 1, r == 4 ? IDXH[idx] : IDXL[idx];
    return R8[r];
}
static const char *rname(int r, int idx, int real) {
    if (r == 6) return used |= idx != 0, NAME8[idx][6];
    if (idx && !real && (r == 4 || r == 5)) used = 1;
    return NAME8[real ? 0 : idx][r];
}
/* BC, DE, HL (or IX, IY), SP as a C expression, and the line that sets it */
static const char *pair_get(int p, int idx) {
    if (p == 2) return used |= idx != 0, IDXGET[idx];
    return PAIRGET[p];
}
static void pair_set(Lines *o, int p, int idx, const char *v) {
    if (p == 3) lines_add(o, "c->sp = %s;", v);
    else lines_add(o, "%s(c, %s);", p == 2 ? (used |= idx != 0, IDXSET[idx]) : PAIRSET[p], v);
}
static const char *pair_name(int p, int idx) { return p == 2 ? (used |= idx != 0, IDXN[idx]) : PAIRN[p]; }

/* The address of (HL), or the lines that read d and make (IX+d) (WZ gets it too): returns the C expression.
 * The caller adds the 5 T-states (or 2) that follow d. */
static const char *mem_ea(Lines *o, int idx) {
    if (!idx) return "z80_hl(c)";
    used = 1;
    lines_add(o, "int8_t d = (int8_t)@F;");
    lines_add(o, "uint16_t ea = (uint16_t)(%s + d);", IDXGET[idx]);
    lines_add(o, "c->wz = ea;");
    return "ea";
}
/* nn, read low then high, into `lo` and `t` */
static void read_nn(Lines *o) {
    lines_add(o, "uint16_t lo = @F;");
    lines_add(o, "uint16_t t = (uint16_t)(lo | @F << 8);");
}
static void push_pc(Lines *o, const char *ind) {
    lines_add(o, "%sz80_wr(c, --c->sp, (uint8_t)(@P >> 8));", ind);
    lines_add(o, "%sz80_wr(c, --c->sp, (uint8_t)@P);", ind);
}

/* ---- CB ---- */
/* The operation of a CB opcode on value v ("v" for memory, else the register); y, x of the opcode. */
static void cb_value(char *e, size_t len, int x, int y, const char *v) {
    if (x == 0) snprintf(e, len, "z80_op_%s(c, %s)", SHIFT[y], v);
    else if (x == 2) snprintf(e, len, "(uint8_t)(%s & ~0x%02X)", v, 1 << y);
    else snprintf(e, len, "(uint8_t)(%s | 0x%02X)", v, 1 << y);
}

static void cb_body(int op, Lines *o, char *name, size_t len) {
    int x = op >> 6, y = (op >> 3) & 7, r = op & 7;
    char e[96];
    if (x == 1) {  /* BIT y,r: (HL) is only read; bits 3 and 5 of the register, or of WZ's high byte for (HL) */
        snprintf(name, len, "BIT %d,%s", y, NAME8[0][r]);
        if (r == 6) {
            lines_add(o, "uint8_t v = rd(c, z80_hl(c));");
            lines_add(o, "z80_tick(c, 1);");
            lines_add(o, "z80_op_BIT(c, %d, v, (uint8_t)(c->wz >> 8));", y);
        } else {
            lines_add(o, "z80_op_BIT(c, %d, %s, %s);", y, R8[r], R8[r]);
        }
        return;
    }
    static const char *const N[4] = {"", "", "RES", "SET"};
    if (x == 0) snprintf(name, len, "%s %s", SHIFT[y], NAME8[0][r]);
    else snprintf(name, len, "%s %d,%s", N[x], y, NAME8[0][r]);
    if (r == 6) {
        cb_value(e, sizeof e, x, y, "v");
        lines_add(o, "uint8_t v = rd(c, z80_hl(c));");
        lines_add(o, "z80_tick(c, 1);");
        lines_add(o, "z80_wr(c, z80_hl(c), %s);", e);
    } else {
        cb_value(e, sizeof e, x, y, R8[r]);
        lines_add(o, "%s = %s;", R8[r], e);
    }
}

/* DD CB d op, FD CB d op: always on (IX+d); the result also goes to register r (r != 6), BIT never writes */
static void ddcb_body(int op, int idx, Lines *o, char *name, size_t len) {
    int x = op >> 6, y = (op >> 3) & 7, r = op & 7;
    const char *m = idx == 1 ? "(IX+d)" : "(IY+d)";
    if (x == 1) {
        snprintf(name, len, "BIT %d,%s", y, m);
        lines_add(o, "uint8_t v = rd(c, ea);");
        lines_add(o, "z80_tick(c, 1);");
        lines_add(o, "z80_op_BIT(c, %d, v, (uint8_t)(ea >> 8));", y);
        return;
    }
    static const char *const N[4] = {"", "", "RES", "SET"};
    char e[96], head[32];
    if (x == 0) snprintf(head, sizeof head, "%s", SHIFT[y]);
    else snprintf(head, sizeof head, "%s %d,", N[x], y);
    if (x == 0) snprintf(name, len, "%s %s%s%s", head, m, r == 6 ? "" : ",", r == 6 ? "" : NAME8[0][r]);
    else snprintf(name, len, "%s%s%s%s", head, m, r == 6 ? "" : ",", r == 6 ? "" : NAME8[0][r]);
    cb_value(e, sizeof e, x, y, "v");
    lines_add(o, "uint8_t v = rd(c, ea);");
    lines_add(o, "z80_tick(c, 1);");
    lines_add(o, "v = %s;", e);
    lines_add(o, "z80_wr(c, ea, v);");
    if (r != 6) lines_add(o, "%s = v;", R8[r]);
}

/* ---- ED ---- */
static void ed_block(int y, int z, Lines *o, char *name, size_t len) {
    static const char *const N[4][4] = {{"LDI", "CPI", "INI", "OUTI"}, {"LDD", "CPD", "IND", "OUTD"},
                                        {"LDIR", "CPIR", "INIR", "OTIR"}, {"LDDR", "CPDR", "INDR", "OTDR"}};
    int dec = y & 1, rep = y >= 6;
    const char *sign = dec ? "-" : "+";
    snprintf(name, len, "%s", N[y - 4][z]);
    if (z == 0) {  /* LDI: copy (HL) to (DE), 2 T-states */
        lines_add(o, "uint8_t v = rd(c, z80_hl(c));");
        lines_add(o, "z80_wr(c, z80_de(c), v);");
        lines_add(o, "z80_tick(c, 2);");
        lines_add(o, "z80_set_hl(c, (uint16_t)(z80_hl(c) %s 1));", sign);
        lines_add(o, "z80_set_de(c, (uint16_t)(z80_de(c) %s 1));", sign);
        lines_add(o, "z80_set_bc(c, (uint16_t)(z80_bc(c) - 1));");
        lines_add(o, "z80_blk_ld(c, v);");
    } else if (z == 1) {  /* CPI */
        lines_add(o, "uint8_t v = rd(c, z80_hl(c));");
        lines_add(o, "z80_tick(c, 5);");
        lines_add(o, "z80_set_hl(c, (uint16_t)(z80_hl(c) %s 1));", sign);
        lines_add(o, "z80_set_bc(c, (uint16_t)(z80_bc(c) - 1));");
        lines_add(o, "z80_blk_cp(c, v);");
        lines_add(o, "c->wz = (uint16_t)(c->wz %s 1);", sign);
    } else if (z == 2) {  /* INI: the port read goes to (HL) */
        lines_add(o, "z80_tick(c, 1);");
        lines_add(o, "uint8_t v = z80_inp(c, z80_bc(c));");
        lines_add(o, "z80_wr(c, z80_hl(c), v);");
        lines_add(o, "c->wz = (uint16_t)(z80_bc(c) %s 1);", sign);
        lines_add(o, "c->b = (uint8_t)(c->b - 1);");
        lines_add(o, "z80_set_hl(c, (uint16_t)(z80_hl(c) %s 1));", sign);
        lines_add(o, "z80_blk_io(c, v, v + (uint8_t)(c->c %s 1));", sign);
    } else {  /* OUTI: (HL) goes to the port, B counted down first */
        lines_add(o, "z80_tick(c, 1);");
        lines_add(o, "uint8_t v = rd(c, z80_hl(c));");
        lines_add(o, "c->b = (uint8_t)(c->b - 1);");
        lines_add(o, "z80_outp(c, z80_bc(c), v);");
        lines_add(o, "c->wz = (uint16_t)(z80_bc(c) %s 1);", sign);
        lines_add(o, "z80_set_hl(c, (uint16_t)(z80_hl(c) %s 1));", sign);
        lines_add(o, "z80_blk_io(c, v, v + c->l);");
    }
    if (!rep) return;
    /* repeat while BC (CPIR: and no match; INIR, OTIR: B) is not 0: 5 more T-states, back to the instruction */
    lines_add(o, "uint16_t pc0 = (uint16_t)(@P - 2);");
    if (z == 0) lines_add(o, "if (z80_bc(c)) {");
    else if (z == 1) lines_add(o, "if (z80_bc(c) && !(c->f & Z80_FLAG_Z)) {");
    else lines_add(o, "if (c->b) {");
    lines_add(o, "    z80_tick(c, 5);");
    lines_add(o, z >= 2 ? "    z80_blk_io_repeat(c, v, pc0);" : "    z80_blk_repeat(c, pc0);");
    lines_add(o, "    @B pc0;");
    lines_add(o, "}");
}

static void ed_body(int op, Lines *o, char *name, size_t len) {
    int x = op >> 6, y = (op >> 3) & 7, z = op & 7, p = y >> 1, q = y & 1;
    if (x == 2 && y >= 4 && z <= 3) {
        ed_block(y, z, o, name, len);
        return;
    }
    if (x != 1) {
        snprintf(name, len, "NOP (ED $%02X)", op);
        return;
    }
    switch (z) {
    case 0:
        if (y == 6) snprintf(name, len, "IN (C)");
        else snprintf(name, len, "IN %s,(C)", NAME8[0][y]);
        lines_add(o, "uint8_t v = z80_inp(c, z80_bc(c));");
        lines_add(o, "c->wz = (uint16_t)(z80_bc(c) + 1);");
        lines_add(o, "z80_in_flags(c, v);");
        if (y != 6) lines_add(o, "%s = v;", R8[y]);
        return;
    case 1:
        snprintf(name, len, "OUT (C),%s", y == 6 ? "0" : NAME8[0][y]);
        lines_add(o, "z80_outp(c, z80_bc(c), %s);", y == 6 ? "0" : R8[y]);
        lines_add(o, "c->wz = (uint16_t)(z80_bc(c) + 1);");
        return;
    case 2: {
        const char *n = q ? "ADC" : "SBC";
        snprintf(name, len, "%s HL,%s", n, PAIRN[p]);
        lines_add(o, "z80_tick(c, 7);");
        lines_add(o, "uint16_t hl = z80_hl(c);");
        lines_add(o, "c->wz = (uint16_t)(hl + 1);");
        lines_add(o, "z80_set_hl(c, z80_%s16(c, hl, %s));", q ? "adc" : "sbc", p == 2 ? "hl" : PAIRGET[p]);
        return;
    }
    case 3:
        snprintf(name, len, q ? "LD %s,(nn)" : "LD (nn),%s", PAIRN[p]);
        read_nn(o);
        if (q) {
            lines_add(o, "uint16_t lo2 = rd(c, t);");
            lines_add(o, "uint16_t t2 = (uint16_t)(lo2 | rd(c, (uint16_t)(t + 1)) << 8);");
            if (p == 3) lines_add(o, "c->sp = t2;");
            else lines_add(o, "%s(c, t2);", p == 2 ? "z80_set_hl" : PAIRSET[p]);
        } else {
            lines_add(o, "uint16_t v = %s;", p == 2 ? "z80_hl(c)" : PAIRGET[p]);
            lines_add(o, "z80_wr(c, t, (uint8_t)v);");
            lines_add(o, "z80_wr(c, (uint16_t)(t + 1), (uint8_t)(v >> 8));");
        }
        lines_add(o, "c->wz = (uint16_t)(t + 1);");
        return;
    case 4:
        snprintf(name, len, "NEG");
        lines_add(o, "z80_op_NEG(c);");
        return;
    case 5:
        snprintf(name, len, y == 1 ? "RETI" : "RETN");
        lines_add(o, "uint16_t lo = rd(c, c->sp++);");
        lines_add(o, "uint16_t t = (uint16_t)(lo | rd(c, c->sp++) << 8);");
        lines_add(o, "c->iff1 = c->iff2;");
        lines_add(o, "c->wz = t;");
        lines_add(o, "@J t;");
        return;
    case 6: {
        static const int IM[4] = {0, 0, 1, 2};
        snprintf(name, len, "IM %d", IM[y & 3]);
        lines_add(o, "c->im = %d;", IM[y & 3]);
        return;
    }
    default:
        switch (y) {
        case 0: snprintf(name, len, "LD I,A"), lines_add(o, "z80_tick(c, 1);"), lines_add(o, "c->i = c->a;"); break;
        case 1: snprintf(name, len, "LD R,A"), lines_add(o, "z80_tick(c, 1);"), lines_add(o, "c->r = c->a;"); break;
        case 2: snprintf(name, len, "LD A,I"), lines_add(o, "z80_tick(c, 1);"), lines_add(o, "z80_ld_a_ir(c, c->i);"); break;
        case 3: snprintf(name, len, "LD A,R"), lines_add(o, "z80_tick(c, 1);"), lines_add(o, "z80_ld_a_ir(c, c->r);"); break;
        case 4: case 5:
            snprintf(name, len, y == 4 ? "RRD" : "RLD");
            lines_add(o, "uint8_t v = rd(c, z80_hl(c));");
            lines_add(o, "z80_tick(c, 4);");
            lines_add(o, "z80_wr(c, z80_hl(c), z80_rxd(c, v, %d));", y == 5);
            lines_add(o, "c->wz = (uint16_t)(z80_hl(c) + 1);");
            break;
        default: snprintf(name, len, "NOP (ED $%02X)", op);
        }
        return;
    }
}

/* ---- the base table (and DD, FD) ---- */
static void base_body(int op, int idx, Lines *o, char *name, size_t len) {
    int x = op >> 6, y = (op >> 3) & 7, z = op & 7, p = y >> 1, q = y & 1;
    char t1[64];
    if (op == 0x76) {
        snprintf(name, len, "HALT");
        lines_add(o, "c->halted = 1;");
        return;
    }
    if (x == 1) {  /* LD r,r' */
        int mem = y == 6 || z == 6;
        snprintf(name, len, "LD %s,%s", rname(y, idx, z == 6), rname(z, idx, y == 6));
        if (mem) {
            const char *ea = mem_ea(o, idx);
            if (idx) lines_add(o, "z80_tick(c, 5);");
            if (y == 6) lines_add(o, "z80_wr(c, %s, %s);", ea, R8[z]);
            else lines_add(o, "%s = rd(c, %s);", R8[y], ea);
        } else {
            lines_add(o, "%s = %s;", reg(y, idx, 0), reg(z, idx, 0));
        }
        return;
    }
    if (x == 2) {  /* ALU A,r */
        snprintf(name, len, "%s A,%s", ALU[y], rname(z, idx, 0));
        if (z == 6) {
            const char *ea = mem_ea(o, idx);
            if (idx) lines_add(o, "z80_tick(c, 5);");
            lines_add(o, "z80_op_%s(c, rd(c, %s));", ALU[y], ea);
        } else {
            lines_add(o, "z80_op_%s(c, %s);", ALU[y], reg(z, idx, 0));
        }
        return;
    }
    if (x == 0) {
        switch (z) {
        case 0:
            if (y == 0) { snprintf(name, len, "NOP"); return; }
            if (y == 1) {
                snprintf(name, len, "EX AF,AF'");
                lines_add(o, "uint16_t t = z80_af(c);");
                lines_add(o, "c->a = (uint8_t)(c->af_ >> 8);");
                lines_add(o, "c->f = (uint8_t)c->af_;");
                lines_add(o, "c->af_ = t;");
                return;
            }
            if (y == 2) {
                snprintf(name, len, "DJNZ e");
                lines_add(o, "z80_tick(c, 1);");
                lines_add(o, "int8_t e = (int8_t)@F;");
                lines_add(o, "c->b = (uint8_t)(c->b - 1);");
                lines_add(o, "if (c->b) {");
                lines_add(o, "    z80_tick(c, 5);");
                lines_add(o, "    c->wz = (uint16_t)(@P + e);");
                lines_add(o, "    @B c->wz;");
                lines_add(o, "}");
                return;
            }
            lines_add(o, "int8_t e = (int8_t)@F;");
            if (y == 3) {
                snprintf(name, len, "JR e");
                lines_add(o, "z80_tick(c, 5);");
                lines_add(o, "c->wz = (uint16_t)(@P + e);");
                lines_add(o, "@J c->wz;");
            } else {
                snprintf(name, len, "JR %s,e", CONDN[y - 4]);
                lines_add(o, "if (%s) {", COND[y - 4]);
                lines_add(o, "    z80_tick(c, 5);");
                lines_add(o, "    c->wz = (uint16_t)(@P + e);");
                lines_add(o, "    @B c->wz;");
                lines_add(o, "}");
            }
            return;
        case 1:
            if (!q) {
                snprintf(name, len, "LD %s,nn", pair_name(p, idx));
                lines_add(o, "uint16_t lo = @F;");
                snprintf(t1, sizeof t1, "(uint16_t)(lo | @F << 8)");
                pair_set(o, p, idx, t1);
            } else {
                snprintf(name, len, "ADD %s,%s", IDXN[idx], pair_name(p, idx));
                used |= idx != 0;
                lines_add(o, "z80_tick(c, 7);");
                lines_add(o, "uint16_t x = %s;", IDXGET[idx]);
                lines_add(o, "c->wz = (uint16_t)(x + 1);");
                lines_add(o, "%s(c, z80_add16(c, x, %s));", IDXSET[idx], p == 2 ? "x" : PAIRGET[p]);
            }
            return;
        case 2: {  /* LD (BC),A / LD A,(BC) ... LD (nn),HL / LD HL,(nn), LD (nn),A / LD A,(nn) */
            if (p < 2) {
                const char *a = p == 0 ? "z80_bc(c)" : "z80_de(c)";
                if (!q) {
                    snprintf(name, len, "LD (%s),A", PAIRN[p]);
                    lines_add(o, "z80_wr(c, %s, c->a);", a);
                    lines_add(o, "c->wz = (uint16_t)(((%s + 1) & 0xFF) | c->a << 8);", a);
                } else {
                    snprintf(name, len, "LD A,(%s)", PAIRN[p]);
                    lines_add(o, "c->a = rd(c, %s);", a);
                    lines_add(o, "c->wz = (uint16_t)(%s + 1);", a);
                }
                return;
            }
            read_nn(o);
            if (p == 2) {
                used |= idx != 0;
                snprintf(name, len, q ? "LD %s,(nn)" : "LD (nn),%s", IDXN[idx]);
                if (q) {
                    lines_add(o, "%s = rd(c, t);", IDXL[idx]);
                    lines_add(o, "%s = rd(c, (uint16_t)(t + 1));", IDXH[idx]);
                } else {
                    lines_add(o, "z80_wr(c, t, %s);", IDXL[idx]);
                    lines_add(o, "z80_wr(c, (uint16_t)(t + 1), %s);", IDXH[idx]);
                }
                lines_add(o, "c->wz = (uint16_t)(t + 1);");
            } else if (!q) {
                snprintf(name, len, "LD (nn),A");
                lines_add(o, "z80_wr(c, t, c->a);");
                lines_add(o, "c->wz = (uint16_t)(((t + 1) & 0xFF) | c->a << 8);");
            } else {
                snprintf(name, len, "LD A,(nn)");
                lines_add(o, "c->a = rd(c, t);");
                lines_add(o, "c->wz = (uint16_t)(t + 1);");
            }
            return;
        }
        case 3:
            snprintf(name, len, "%s %s", q ? "DEC" : "INC", pair_name(p, idx));
            lines_add(o, "z80_tick(c, 2);");
            snprintf(t1, sizeof t1, "(uint16_t)(%s %s 1)", pair_get(p, idx), q ? "-" : "+");
            pair_set(o, p, idx, t1);
            return;
        case 4: case 5: {
            const char *mn = z == 4 ? "INC" : "DEC";
            snprintf(name, len, "%s %s", mn, rname(y, idx, 0));
            if (y == 6) {
                const char *ea = mem_ea(o, idx);
                if (idx) lines_add(o, "z80_tick(c, 5);");
                lines_add(o, "uint8_t v = rd(c, %s);", ea);
                lines_add(o, "z80_tick(c, 1);");
                lines_add(o, "z80_wr(c, %s, z80_op_%s(c, v));", ea, mn);
            } else {
                const char *r = reg(y, idx, 0);
                lines_add(o, "%s = z80_op_%s(c, %s);", r, mn, r);
            }
            return;
        }
        case 6:
            snprintf(name, len, "LD %s,n", rname(y, idx, 0));
            if (y == 6) {
                const char *ea = mem_ea(o, idx);
                lines_add(o, "uint8_t v = @F;");
                if (idx) lines_add(o, "z80_tick(c, 2);");
                lines_add(o, "z80_wr(c, %s, v);", ea);
            } else {
                lines_add(o, "%s = @F;", reg(y, idx, 0));
            }
            return;
        default: {  /* z == 7 */
            static const char *const N[8] = {"RLCA", "RRCA", "RLA", "RRA", "DAA", "CPL", "SCF", "CCF"};
            snprintf(name, len, "%s", N[y]);
            lines_add(o, "z80_op_%s(c);", N[y]);
            return;
        }
        }
    }
    /* x == 3 */
    switch (z) {
    case 0:
        snprintf(name, len, "RET %s", CONDN[y]);
        lines_add(o, "z80_tick(c, 1);");
        lines_add(o, "if (%s) {", COND[y]);
        lines_add(o, "    uint16_t lo = rd(c, c->sp++);");
        lines_add(o, "    uint16_t t = (uint16_t)(lo | rd(c, c->sp++) << 8);");
        lines_add(o, "    c->wz = t;");
        lines_add(o, "    @B t;");
        lines_add(o, "}");
        return;
    case 1:
        if (!q) {
            snprintf(name, len, "POP %s", p == 3 ? "AF" : pair_name(p, idx));
            if (p == 2 && idx) used = 1;
            lines_add(o, "%s = rd(c, c->sp++);", p == 2 ? IDXL[idx] : LO[p]);
            lines_add(o, "%s = rd(c, c->sp++);", p == 2 ? IDXH[idx] : HI[p]);
        } else if (p == 0) {
            snprintf(name, len, "RET");
            lines_add(o, "uint16_t lo = rd(c, c->sp++);");
            lines_add(o, "uint16_t t = (uint16_t)(lo | rd(c, c->sp++) << 8);");
            lines_add(o, "c->wz = t;");
            lines_add(o, "@J t;");
        } else if (p == 1) {
            snprintf(name, len, "EXX");
            lines_add(o, "uint16_t t = z80_bc(c);");
            lines_add(o, "z80_set_bc(c, c->bc_);");
            lines_add(o, "c->bc_ = t;");
            lines_add(o, "t = z80_de(c);");
            lines_add(o, "z80_set_de(c, c->de_);");
            lines_add(o, "c->de_ = t;");
            lines_add(o, "t = z80_hl(c);");
            lines_add(o, "z80_set_hl(c, c->hl_);");
            lines_add(o, "c->hl_ = t;");
        } else if (p == 2) {
            snprintf(name, len, "JP (%s)", IDXN[idx]);
            used |= idx != 0;
            lines_add(o, "@J %s;", IDXGET[idx]);
        } else {
            snprintf(name, len, "LD SP,%s", IDXN[idx]);
            used |= idx != 0;
            lines_add(o, "z80_tick(c, 2);");
            lines_add(o, "c->sp = %s;", IDXGET[idx]);
        }
        return;
    case 2:
        snprintf(name, len, "JP %s,nn", CONDN[y]);
        read_nn(o);
        lines_add(o, "c->wz = t;");
        lines_add(o, "if (%s) {", COND[y]);
        lines_add(o, "    @B t;");
        lines_add(o, "}");
        return;
    case 3:
        switch (y) {
        case 0:
            snprintf(name, len, "JP nn");
            read_nn(o);
            lines_add(o, "c->wz = t;");
            lines_add(o, "@J t;");
            break;
        case 2:
            snprintf(name, len, "OUT (n),A");
            lines_add(o, "uint8_t n = @F;");
            lines_add(o, "z80_outp(c, (uint16_t)(c->a << 8 | n), c->a);");
            lines_add(o, "c->wz = (uint16_t)(((n + 1) & 0xFF) | c->a << 8);");
            break;
        case 3:
            snprintf(name, len, "IN A,(n)");
            lines_add(o, "uint8_t n = @F;");
            lines_add(o, "uint16_t port = (uint16_t)(c->a << 8 | n);");
            lines_add(o, "c->a = z80_inp(c, port);");
            lines_add(o, "c->wz = (uint16_t)(port + 1);");
            break;
        case 4:
            snprintf(name, len, "EX (SP),%s", IDXN[idx]);
            used |= idx != 0;
            lines_add(o, "uint16_t lo = rd(c, c->sp);");
            lines_add(o, "uint16_t t = (uint16_t)(lo | rd(c, (uint16_t)(c->sp + 1)) << 8);");
            lines_add(o, "z80_tick(c, 1);");
            lines_add(o, "z80_wr(c, (uint16_t)(c->sp + 1), %s);", IDXH[idx]);
            lines_add(o, "z80_wr(c, c->sp, %s);", IDXL[idx]);
            lines_add(o, "z80_tick(c, 2);");
            lines_add(o, "%s(c, t);", IDXSET[idx]);
            lines_add(o, "c->wz = t;");
            break;
        case 5:
            snprintf(name, len, "EX DE,HL");
            lines_add(o, "uint16_t t = z80_de(c);");
            lines_add(o, "z80_set_de(c, z80_hl(c));");
            lines_add(o, "z80_set_hl(c, t);");
            break;
        case 6:
            snprintf(name, len, "DI");
            lines_add(o, "c->iff1 = c->iff2 = 0;");
            break;
        default:
            snprintf(name, len, "EI");
            lines_add(o, "c->iff1 = c->iff2 = 1;");
            lines_add(o, "c->ei = 1;");
        }
        return;
    case 4:
        snprintf(name, len, "CALL %s,nn", CONDN[y]);
        read_nn(o);
        lines_add(o, "c->wz = t;");
        lines_add(o, "if (%s) {", COND[y]);
        lines_add(o, "    z80_tick(c, 1);");
        push_pc(o, "    ");
        lines_add(o, "    @B t;");
        lines_add(o, "}");
        return;
    case 5:
        if (q == 0) {
            snprintf(name, len, "PUSH %s", p == 3 ? "AF" : pair_name(p, idx));
            if (p == 2 && idx) used = 1;
            lines_add(o, "z80_tick(c, 1);");
            lines_add(o, "z80_wr(c, --c->sp, %s);", p == 2 ? IDXH[idx] : HI[p]);
            lines_add(o, "z80_wr(c, --c->sp, %s);", p == 2 ? IDXL[idx] : LO[p]);
            return;
        }
        snprintf(name, len, "CALL nn");
        read_nn(o);
        lines_add(o, "c->wz = t;");
        lines_add(o, "z80_tick(c, 1);");
        push_pc(o, "");
        lines_add(o, "@J t;");
        return;
    case 6:
        snprintf(name, len, "%s A,n", ALU[y]);
        lines_add(o, "z80_op_%s(c, @F);", ALU[y]);
        return;
    default:
        snprintf(name, len, "RST $%02X", y * 8);
        lines_add(o, "z80_tick(c, 1);");
        push_pc(o, "");
        lines_add(o, "c->wz = 0x%04X;", y * 8);
        lines_add(o, "@J 0x%04X;", y * 8);
        return;
    }
}

int z80_body(int table, int op, Lines *o, char *name, size_t len) {
    o->n = 0;
    name[0] = 0;
    used = 0;
    switch (table) {
    case Z80_T_BASE:
        if (op == 0xCB || op == 0xDD || op == 0xED || op == 0xFD) {
            snprintf(name, len, "PREFIX %s", op == 0xCB ? "CB" : op == 0xED ? "ED" : op == 0xDD ? "DD" : "FD");
            return 0;
        }
        base_body(op, 0, o, name, len);
        return 1;
    case Z80_T_CB: cb_body(op, o, name, len); return 1;
    case Z80_T_ED: ed_body(op, o, name, len); return 1;
    case Z80_T_DD: case Z80_T_FD:
        if (op == 0xCB || op == 0xDD || op == 0xED || op == 0xFD) return 0;
        base_body(op, table == Z80_T_DD ? 1 : 2, o, name, len);
        if (!used) o->n = 0;
        return used;
    default:
        ddcb_body(op, table == Z80_T_DDCB ? 1 : 2, o, name, len);
        return 1;
    }
}

static void emit_table(Buf *o, int table, const char *indent) {
    Lines l;
    char name[48];
    for (int i = 0; i < 256; i++) {
        if (!z80_body(table, i, &l, name, sizeof name)) continue;
        bprintf(o, "%scase 0x%02X: { /* %s */\n", indent, i, name);
        for (int k = 0; k < l.n; k++) {
            char ind[16];
            snprintf(ind, sizeof ind, "%s    ", indent);
            template_plain(o, ind, l.s[k]);
        }
        bprintf(o, "%s} break;\n", indent);
    }
}

void z80_cpu_switch(Buf *o) {
    bprintf(o, "/* generated by z80_gen from cpu/z80/z80.c; do not edit */\n");
    emit_table(o, Z80_T_BASE, "");
    bprintf(o, "case 0xCB: switch (z80_m1(c)) {\n");
    emit_table(o, Z80_T_CB, "");
    bprintf(o, "} break;\n");
    bprintf(o, "case 0xED: switch (z80_m1(c)) {\n");
    emit_table(o, Z80_T_ED, "");
    bprintf(o, "} break;\n");
    /* DD and FD: the opcodes that use HL; the rest, and a second prefix, run as the base table's */
    for (int t = Z80_T_DD; t <= Z80_T_FD; t++) {
        int idx = t == Z80_T_DD ? 1 : 2;
        bprintf(o, "case 0x%02X: op = z80_m1(c); c->qprev = 0; switch (op) {\n", t == Z80_T_DD ? 0xDD : 0xFD);
        emit_table(o, t, "");
        bprintf(o, "case 0xCB: {\n");
        bprintf(o, "    int8_t d = (int8_t)rd(c, c->pc++);\n");
        bprintf(o, "    uint8_t op2 = rd(c, c->pc++);\n");
        bprintf(o, "    z80_tick(c, 2);\n");
        bprintf(o, "    uint16_t ea = (uint16_t)(%s(c) + d);\n", idx == 1 ? "z80_ix" : "z80_iy");
        bprintf(o, "    c->wz = ea;\n");
        bprintf(o, "    switch (op2) {\n");
        emit_table(o, t == Z80_T_DD ? Z80_T_DDCB : Z80_T_FDCB, "    ");
        bprintf(o, "    }\n");
        bprintf(o, "} break;\n");
        bprintf(o, "default: goto again;\n");
        bprintf(o, "} break;\n");
    }
}
