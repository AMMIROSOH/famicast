/* C code for one SM83 instruction (core/recomp/template.h markers), from
 * the opcode's bit fields (Pan Docs "CPU instruction set", gbdev "Opcode
 * table"). Used for the interpreter's switch (sm83_gen) and,
 * later, recompiled blocks, so the semantics exist once.
 *
 * Every rd()/wr()/idle() is one M-cycle, in the order the CPU does them
 * (checked cycle by cycle against SingleStepTests sm83). The opcode fetch is
 * done by the caller; a CB opcode's second byte is fetched by the CB case.
 * Helpers (rd, wr, idle, register pairs, op_*) live in sm83_ops.h.
 * C leaves the order of a call's arguments open: no line has two bus
 * accesses. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sm83.h"


static const char *const R8[8] = {"c->b", "c->c", "c->d", "c->e", "c->h", "c->l", "(hl)", "c->a"};
static const char *const R8N[8] = {"B", "C", "D", "E", "H", "L", "(HL)", "A"};
static const char *const ALU[8] = {"ADD", "ADC", "SUB", "SBC", "AND", "XOR", "OR", "CP"};
static const char *const SHIFT[8] = {"RLC", "RRC", "RL", "RR", "SLA", "SRA", "SWAP", "SRL"};
static const char *const COND[4] = {"!(c->f & FLAG_Z)", "(c->f & FLAG_Z)", "!(c->f & FLAG_C)", "(c->f & FLAG_C)"};
static const char *const CONDN[4] = {"NZ", "Z", "NC", "C"};
/* rr for LD/INC/DEC/ADD (BC DE HL SP) and PUSH/POP (BC DE HL AF): high, low */
static const char *const PAIR[4] = {"bc", "de", "hl", "sp"}, *const PAIRN[4] = {"BC", "DE", "HL", "SP"};
static const char *const HI[4] = {"c->b", "c->d", "c->h", "c->a"}, *const LO[4] = {"c->c", "c->e", "c->l", "c->f"};

static int unused(int op) {
    return op == 0xD3 || op == 0xDB || op == 0xDD || op == 0xE3 || op == 0xE4 || op == 0xEB ||
           op == 0xEC || op == 0xED || op == 0xF4 || op == 0xFC || op == 0xFD;
}

int sm83_size(int op) {
    static const unsigned char TWO[] = {0x06, 0x0E, 0x16, 0x1E, 0x26, 0x2E, 0x36, 0x3E, 0x18, 0x20, 0x28,
                                        0x30, 0x38, 0xC6, 0xCE, 0xD6, 0xDE, 0xE6, 0xEE, 0xF6, 0xFE, 0xE0,
                                        0xF0, 0xE8, 0xF8, 0xCB};
    static const unsigned char THREE[] = {0x01, 0x11, 0x21, 0x31, 0x08, 0xC2, 0xC3, 0xCA, 0xD2, 0xDA,
                                          0xC4, 0xCC, 0xCD, 0xD4, 0xDC, 0xEA, 0xFA};
    if (memchr(TWO, op, sizeof TWO)) return 2;
    if (memchr(THREE, op, sizeof THREE)) return 3;
    return 1;
}

/* Lines that leave register r's value in v (reading (HL) is a bus cycle). */
static const char *src(Lines *o, int r) {
    if (r != 6) return R8[r];
    lines_add(o, "uint8_t v = rd(c, hl(c));");
    return "v";
}

static void cb_body(int op, Lines *o, char *name, size_t len) {
    int x = op >> 6, y = (op >> 3) & 7, r = op & 7;
    if (x == 1) {  /* BIT y,r: (HL) is read only */
        snprintf(name, len, "BIT %d,%s", y, R8N[r]);
        lines_add(o, "op_BIT(c, %d, %s);", y, src(o, r));
        return;
    }
    char expr[64];
    if (x == 0) snprintf(name, len, "%s %s", SHIFT[y], R8N[r]), snprintf(expr, sizeof expr, "op_%s(c, %%s)", SHIFT[y]);
    else if (x == 2) snprintf(name, len, "RES %d,%s", y, R8N[r]), snprintf(expr, sizeof expr, "(uint8_t)(%%s & ~0x%02X)", 1 << y);
    else snprintf(name, len, "SET %d,%s", y, R8N[r]), snprintf(expr, sizeof expr, "(uint8_t)(%%s | 0x%02X)", 1 << y);
    char e[96];
    snprintf(e, sizeof e, expr, r == 6 ? "v" : R8[r]);
    if (r == 6) {
        lines_add(o, "uint8_t v = rd(c, hl(c));");
        lines_add(o, "wr(c, hl(c), %s);", e);
    } else {
        lines_add(o, "%s = %s;", R8[r], e);
    }
}

void sm83_body(int op, int cb, Lines *o, char *name, size_t len) {
    o->n = 0;
    if (cb) {
        cb_body(op, o, name, len);
        return;
    }
    int x = op >> 6, y = (op >> 3) & 7, z = op & 7, p = y >> 1, q = y & 1;
    if (unused(op)) {
        snprintf(name, len, "(unused $%02X)", op);
        lines_add(o, "c->jammed = 1;");
        return;
    }
    if (op == 0x76) {
        snprintf(name, len, "HALT");
        lines_add(o, "c->halted = 1;");
        return;
    }
    if (x == 1) {  /* LD r,r' */
        snprintf(name, len, "LD %s,%s", R8N[y], R8N[z]);
        if (y == 6) lines_add(o, "wr(c, hl(c), %s);", R8[z]);
        else if (z == 6) lines_add(o, "%s = rd(c, hl(c));", R8[y]);
        else lines_add(o, "%s = %s;", R8[y], R8[z]);
        return;
    }
    if (x == 2) {  /* ALU A,r */
        snprintf(name, len, "%s A,%s", ALU[y], R8N[z]);
        lines_add(o, "op_%s(c, %s);", ALU[y], src(o, z));
        return;
    }
    if (x == 0) {
        switch (z) {
        case 0:
            if (y == 0) { snprintf(name, len, "NOP"); return; }
            if (y == 1) {
                snprintf(name, len, "LD (nn),SP");
                lines_add(o, "uint16_t lo = @F;");
                lines_add(o, "uint16_t a = lo | @F << 8;");
                lines_add(o, "wr(c, a, (uint8_t)c->sp);");
                lines_add(o, "wr(c, (uint16_t)(a + 1), (uint8_t)(c->sp >> 8));");
                return;
            }
            if (y == 2) { snprintf(name, len, "STOP"); lines_add(o, "c->stopped = 1;"); return; }
            lines_add(o, "int8_t e = (int8_t)@F;");
            if (y == 3) {
                snprintf(name, len, "JR e");
                lines_add(o, "idle(c);");
                lines_add(o, "@J (uint16_t)(@P + e);");
            } else {
                snprintf(name, len, "JR %s,e", CONDN[y - 4]);
                lines_add(o, "if (%s) {", COND[y - 4]);
                lines_add(o, "    idle(c);");
                lines_add(o, "    @B (uint16_t)(@P + e);");
                lines_add(o, "}");
            }
            return;
        case 1:
            if (!q) {
                snprintf(name, len, "LD %s,nn", p == 3 ? "SP" : PAIRN[p]);
                lines_add(o, "uint16_t lo = @F;");
                if (p == 3) lines_add(o, "c->sp = (uint16_t)(lo | @F << 8);");
                else lines_add(o, "set_%s(c, (uint16_t)(lo | @F << 8));", PAIR[p]);
            } else {
                snprintf(name, len, "ADD HL,%s", p == 3 ? "SP" : PAIRN[p]);
                lines_add(o, "idle(c);");
                lines_add(o, "op_add_hl(c, %s);", p == 3 ? "c->sp" : p == 0 ? "bc(c)" : p == 1 ? "de(c)" : "hl(c)");
            }
            return;
        case 2: {  /* LD (BC),A / LD A,(BC) ... (HL+), (HL-) */
            static const char *const N[4] = {"(BC)", "(DE)", "(HL+)", "(HL-)"};
            const char *a = p == 0 ? "bc(c)" : p == 1 ? "de(c)" : "hl(c)";
            if (!q) snprintf(name, len, "LD %s,A", N[p]), lines_add(o, "wr(c, %s, c->a);", a);
            else snprintf(name, len, "LD A,%s", N[p]), lines_add(o, "c->a = rd(c, %s);", a);
            if (p >= 2) lines_add(o, "set_hl(c, (uint16_t)(hl(c) %s 1));", p == 2 ? "+" : "-");
            return;
        }
        case 3:
            snprintf(name, len, "%s %s", q ? "DEC" : "INC", p == 3 ? "SP" : PAIRN[p]);
            lines_add(o, "idle(c);");
            if (p == 3) lines_add(o, "c->sp%s;", q ? "--" : "++");
            else lines_add(o, "set_%s(c, (uint16_t)(%s(c) %s 1));", PAIR[p], PAIR[p], q ? "-" : "+");
            return;
        case 4: case 5: {
            const char *mn = z == 4 ? "INC" : "DEC";
            snprintf(name, len, "%s %s", mn, R8N[y]);
            if (y == 6) {
                lines_add(o, "uint8_t v = rd(c, hl(c));");
                lines_add(o, "wr(c, hl(c), op_%s(c, v));", mn);
            } else {
                lines_add(o, "%s = op_%s(c, %s);", R8[y], mn, R8[y]);
            }
            return;
        }
        case 6:
            snprintf(name, len, "LD %s,n", R8N[y]);
            if (y == 6) {
                lines_add(o, "uint8_t v = @F;");
                lines_add(o, "wr(c, hl(c), v);");
            } else {
                lines_add(o, "%s = @F;", R8[y]);
            }
            return;
        default: {  /* z == 7 */
            static const char *const N[8] = {"RLCA", "RRCA", "RLA", "RRA", "DAA", "CPL", "SCF", "CCF"};
            snprintf(name, len, "%s", N[y]);
            if (y < 4) lines_add(o, "c->a = op_%s(c, c->a);", SHIFT[y]), lines_add(o, "c->f &= (uint8_t)~FLAG_Z;");
            else if (y == 4) lines_add(o, "op_DAA(c);");
            else if (y == 5) lines_add(o, "c->a = (uint8_t)~c->a;"), lines_add(o, "c->f |= FLAG_N | FLAG_H;");
            else if (y == 6) lines_add(o, "c->f = (uint8_t)((c->f & FLAG_Z) | FLAG_C);");
            else lines_add(o, "c->f = (uint8_t)((c->f & (FLAG_Z | FLAG_C)) ^ FLAG_C);");
            return;
        }
        }
    }
    /* x == 3 */
    switch (z) {
    case 0:
        if (y < 4) {
            snprintf(name, len, "RET %s", CONDN[y]);
            lines_add(o, "idle(c);");
            lines_add(o, "if (%s) {", COND[y]);
            lines_add(o, "    uint16_t lo = rd(c, c->sp++);");
            lines_add(o, "    uint16_t t = lo | rd(c, c->sp++) << 8;");
            lines_add(o, "    idle(c);");
            lines_add(o, "    @B t;");
            lines_add(o, "}");
        } else if (y == 4 || y == 6) {
            snprintf(name, len, y == 4 ? "LDH (n),A" : "LDH A,(n)");
            lines_add(o, "uint8_t n = @F;");
            if (y == 4) lines_add(o, "wr(c, 0xFF00 | n, c->a);");
            else lines_add(o, "c->a = rd(c, 0xFF00 | n);");
        } else if (y == 5) {
            snprintf(name, len, "ADD SP,e");
            lines_add(o, "uint8_t e = @F;");
            lines_add(o, "idle(c);");
            lines_add(o, "idle(c);");
            lines_add(o, "c->sp = op_sp_e(c, e);");
        } else {
            snprintf(name, len, "LD HL,SP+e");
            lines_add(o, "uint8_t e = @F;");
            lines_add(o, "idle(c);");
            lines_add(o, "set_hl(c, op_sp_e(c, e));");
        }
        return;
    case 1:
        if (!q) {
            snprintf(name, len, "POP %s", p == 3 ? "AF" : PAIRN[p]);
            lines_add(o, "%s = rd(c, c->sp++);", LO[p]);
            lines_add(o, "%s = rd(c, c->sp++);", HI[p]);
            if (p == 3) lines_add(o, "c->f &= 0xF0;");
        } else if (p == 0 || p == 1) {
            snprintf(name, len, p ? "RETI" : "RET");
            lines_add(o, "uint16_t lo = rd(c, c->sp++);");
            lines_add(o, "uint16_t t = lo | rd(c, c->sp++) << 8;");
            lines_add(o, "idle(c);");
            lines_add(o, "@J t;");
            if (p) lines_add(o, "c->ime = 1;");
        } else if (p == 2) {
            snprintf(name, len, "JP HL");
            lines_add(o, "@J hl(c);");
        } else {
            snprintf(name, len, "LD SP,HL");
            lines_add(o, "idle(c);");
            lines_add(o, "c->sp = hl(c);");
        }
        return;
    case 2:
        if (y < 4) {
            snprintf(name, len, "JP %s,nn", CONDN[y]);
            lines_add(o, "uint16_t lo = @F;");
            lines_add(o, "uint16_t t = lo | @F << 8;");
            lines_add(o, "if (%s) {", COND[y]);
            lines_add(o, "    idle(c);");
            lines_add(o, "    @B t;");
            lines_add(o, "}");
        } else if (y == 4 || y == 6) {
            snprintf(name, len, y == 4 ? "LD (C),A" : "LD A,(C)");
            if (y == 4) lines_add(o, "wr(c, 0xFF00 | c->c, c->a);");
            else lines_add(o, "c->a = rd(c, 0xFF00 | c->c);");
        } else {
            snprintf(name, len, y == 5 ? "LD (nn),A" : "LD A,(nn)");
            lines_add(o, "uint16_t lo = @F;");
            lines_add(o, "uint16_t a = lo | @F << 8;");
            if (y == 5) lines_add(o, "wr(c, a, c->a);");
            else lines_add(o, "c->a = rd(c, a);");
        }
        return;
    case 3:
        if (y == 0) {
            snprintf(name, len, "JP nn");
            lines_add(o, "uint16_t lo = @F;");
            lines_add(o, "uint16_t t = lo | @F << 8;");
            lines_add(o, "idle(c);");
            lines_add(o, "@J t;");
        } else if (y == 1) {
            snprintf(name, len, "PREFIX CB");  /* the CB case of the switch, see sm83.h */
        } else {
            snprintf(name, len, y == 6 ? "DI" : "EI");
            if (y == 6) lines_add(o, "c->ime = 0;"), lines_add(o, "c->ei = 0;");
            else lines_add(o, "c->ei = 1;");
        }
        return;
    case 4: case 5:
        if (z == 5 && q == 0) {
            snprintf(name, len, "PUSH %s", p == 3 ? "AF" : PAIRN[p]);
            lines_add(o, "idle(c);");
            lines_add(o, "wr(c, --c->sp, %s);", HI[p]);
            lines_add(o, "wr(c, --c->sp, %s);", LO[p]);
            return;
        }
        if (z == 4) snprintf(name, len, "CALL %s,nn", CONDN[y]);
        else snprintf(name, len, "CALL nn");
        lines_add(o, "uint16_t lo = @F;");
        lines_add(o, "uint16_t t = lo | @F << 8;");
        if (z == 4) lines_add(o, "if (%s) {", COND[y]);
        lines_add(o, "%sidle(c);", z == 4 ? "    " : "");
        lines_add(o, "%swr(c, --c->sp, (uint8_t)(@P >> 8));", z == 4 ? "    " : "");
        lines_add(o, "%swr(c, --c->sp, (uint8_t)@P);", z == 4 ? "    " : "");
        if (z == 4) lines_add(o, "    @B t;"), lines_add(o, "}");
        else lines_add(o, "@J t;");
        return;
    case 6:
        snprintf(name, len, "%s A,n", ALU[y]);
        lines_add(o, "op_%s(c, @F);", ALU[y]);
        return;
    default:
        snprintf(name, len, "RST $%02X", y * 8);
        lines_add(o, "idle(c);");
        lines_add(o, "wr(c, --c->sp, (uint8_t)(@P >> 8));");
        lines_add(o, "wr(c, --c->sp, (uint8_t)@P);");
        lines_add(o, "@J 0x%04X;", y * 8);
        return;
    }
}

void sm83_cpu_switch(Buf *o) {
    bprintf(o, "/* generated by sm83_gen from cpu/sm83/sm83.c; do not edit */\n");
    Lines l;
    char name[32];
    for (int cb = 0; cb < 2; cb++) {
        if (cb) bprintf(o, "case 0xCB: switch (rd(c, c->pc++)) {\n");
        for (int i = 0; i < 256; i++) {
            if (!cb && i == 0xCB) continue;
            sm83_body(i, cb, &l, name, sizeof name);
            bprintf(o, "case 0x%02X: { /* %s */\n", i, name);
            for (int k = 0; k < l.n; k++) template_plain(o, "    ", l.s[k]);
            bprintf(o, "} break;\n");
        }
        if (cb) bprintf(o, "} break;\n");
    }
}
