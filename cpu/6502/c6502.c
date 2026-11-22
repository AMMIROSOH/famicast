/* C code for one 6502 instruction, from the opcode table (ops.c).
 *
 * Used for both the interpreter's switch (cpu6502_gen) and the
 * recompiler's block functions, so the instruction semantics exist once and
 * are verified once (nestest + SingleStepTests, systems/nes/docs/verification-log.md).
 *
 * Every rd()/wr() is one CPU cycle, including the dummy reads and writes the
 * real 2A03 performs (sequences from 6502_cpu.txt / nesdev; checked cycle by
 * cycle against SingleStepTests). The opcode fetch is done by the caller.
 * Helpers (rd, wr, push, pull, op_*) live in cpu6502_ops.h.
 *
 * Every use of the PC is a marker, never C text (core/recomp/template.h),
 * so the emitter can make it a constant. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "c6502.h"


static int is(const char *mn, const char *list) {
    size_t n = strlen(mn);
    for (const char *p = list; (p = strstr(p, mn)); p += n)
        if ((p == list || p[-1] == ' ') && (p[n] == ' ' || p[n] == 0)) return 1;
    return 0;
}

#define READ "LDA LDX LDY ADC SBC AND ORA EOR CMP CPX CPY BIT LAX NOP LAS ANC ALR ARR AXS XAA"
#define RMW "ASL LSR ROL ROR INC DEC SLO RLA SRE RRA DCP ISB"

static const char *const IMPLIED[][2] = {
    {"CLC", "c->p &= ~FLAG_C;"}, {"SEC", "c->p |= FLAG_C;"},
    {"CLI", "c->p &= ~FLAG_I;"}, {"SEI", "c->p |= FLAG_I;"},
    {"CLD", "c->p &= ~FLAG_D;"}, {"SED", "c->p |= FLAG_D;"}, {"CLV", "c->p &= ~FLAG_V;"},
    {"TAX", "c->x = c->a; nz(c, c->x);"}, {"TAY", "c->y = c->a; nz(c, c->y);"},
    {"TXA", "c->a = c->x; nz(c, c->a);"}, {"TYA", "c->a = c->y; nz(c, c->a);"},
    {"TSX", "c->x = c->s; nz(c, c->x);"}, {"TXS", "c->s = c->x;"},
    {"INX", "c->x++; nz(c, c->x);"}, {"INY", "c->y++; nz(c, c->y);"},
    {"DEX", "c->x--; nz(c, c->x);"}, {"DEY", "c->y--; nz(c, c->y);"},
    {"NOP", ""},
};

static const char *const BRANCH_COND[][2] = {
    {"BPL", "!(c->p & FLAG_N)"}, {"BMI", "(c->p & FLAG_N)"},
    {"BVC", "!(c->p & FLAG_V)"}, {"BVS", "(c->p & FLAG_V)"},
    {"BCC", "!(c->p & FLAG_C)"}, {"BCS", "(c->p & FLAG_C)"},
    {"BNE", "!(c->p & FLAG_Z)"}, {"BEQ", "(c->p & FLAG_Z)"},
};

static const char *const STORE[][2] = {
    {"STA", "c->a"}, {"STX", "c->x"}, {"STY", "c->y"}, {"SAX", "c->a & c->x"},
};

/* SHX/SHY/AHX/TAS: value & (high byte + 1), see unstable_store */
static const char *const UNSTABLE[][2] = {
    {"SHY", "c->y"}, {"SHX", "c->x"}, {"AHX", "c->a & c->x"}, {"TAS", "c->s"},
};

static const char *const SPECIAL[][12] = {
    {"JSR", "uint16_t lo = @F;", "rd(c, 0x100 | c->s);",
     "push(c, @P >> 8);", "push(c, @P & 0xFF);",
     "@J lo | @R << 8;"},
    {"RTS", "@R;", "rd(c, 0x100 | c->s);", "uint16_t lo = pull(c);",
     "@J lo | pull(c) << 8;", "@R;", "@I;"},
    {"RTI", "@R;", "rd(c, 0x100 | c->s);", "c->p = (pull(c) & ~FLAG_B) | FLAG_U;",
     "uint16_t lo = pull(c);", "@J lo | pull(c) << 8;"},
    /* An NMI seen in BRK's first four cycles takes over its vector; B is still
     * pushed set. The sequence does not poll at its end, so the handler's first
     * instruction always runs (nesdev "CPU interrupts", cpu_interrupts_v2 2). */
    {"BRK", "@F;", "push(c, @P >> 8);", "push(c, @P & 0xFF);",
     "uint16_t vec = c->nmi ? 0xFFFA : 0xFFFE;", "if (vec == 0xFFFA) c->nmi = 0;",
     "push(c, c->p | FLAG_B | FLAG_U);", "c->p |= FLAG_I;",
     "uint16_t lo = rd(c, vec);", "@J lo | rd(c, vec + 1) << 8;",
     "c->poll_prev = 0;"},
    {"PHA", "@R;", "push(c, c->a);"},
    {"PHP", "@R;", "push(c, c->p | FLAG_B | FLAG_U);"},
    {"PLA", "@R;", "rd(c, 0x100 | c->s);", "c->a = pull(c);", "nz(c, c->a);"},
    {"PLP", "@R;", "rd(c, 0x100 | c->s);", "c->p = (pull(c) & ~FLAG_B) | FLAG_U;"},
    {"KIL", "c->jammed = 1;"},
};

#define FIND(table, key) find((const char *const *)(table), sizeof(table) / sizeof(table[0]), \
                              sizeof(table[0]) / sizeof(char *), key)

static const char *const *find(const char *const *t, size_t rows, size_t cols, const char *key) {
    for (size_t r = 0; r < rows; r++)
        if (!strcmp(t[r * cols], key)) return t + r * cols;
    return NULL;
}

/* Lines that leave the effective address in `ea`. Indexed modes read the
 * un-carried address first when the index crosses a page (reads), or always
 * (writes and read-modify-write). */
static void ea(Lines *o, const char *mode, int read) {
    const char *fix = read ? "if ((ea ^ base) & 0xFF00) rd(c, (base & 0xFF00) | (ea & 0xFF));"
                           : "rd(c, (base & 0xFF00) | (ea & 0xFF));";
    if (!strcmp(mode, "ZP")) {
        lines_add(o, "uint16_t ea = @F;");
    } else if (!strcmp(mode, "ZPX") || !strcmp(mode, "ZPY")) {
        lines_add(o, "uint8_t zp = @F;");
        lines_add(o, "rd(c, zp);");
        lines_add(o, "uint16_t ea = (uint8_t)(zp + c->%c);", mode[2] == 'X' ? 'x' : 'y');
    } else if (!strcmp(mode, "ABS")) {
        lines_add(o, "uint16_t ea = @F;");
        lines_add(o, "ea |= @F << 8;");
    } else if (!strcmp(mode, "ABX") || !strcmp(mode, "ABY")) {
        lines_add(o, "uint16_t base = @F;");
        lines_add(o, "base |= @F << 8;");
        lines_add(o, "uint16_t ea = base + c->%c;", mode[2] == 'X' ? 'x' : 'y');
        lines_add(o, "%s", fix);
    } else if (!strcmp(mode, "IZX")) {
        lines_add(o, "uint8_t zp = @F;");
        lines_add(o, "rd(c, zp);");
        lines_add(o, "zp += c->x;");
        lines_add(o, "uint16_t ea = rd(c, zp);");
        lines_add(o, "ea |= rd(c, (uint8_t)(zp + 1)) << 8;");
    } else if (!strcmp(mode, "IZY")) {
        lines_add(o, "uint8_t zp = @F;");
        lines_add(o, "uint16_t base = rd(c, zp);");
        lines_add(o, "base |= rd(c, (uint8_t)(zp + 1)) << 8;");
        lines_add(o, "uint16_t ea = base + c->y;");
        lines_add(o, "%s", fix);
    } else {
        fprintf(stderr, "c6502: no addressing for %s\n", mode);
        exit(1);
    }
}

void body(const Op *op, Lines *o) {
    const char *mn = op->mn, *mode = op->mode;
    const char *const *row;
    o->n = 0;
    if ((row = FIND(SPECIAL, mn))) {
        for (int i = 1; i < 12 && row[i]; i++) lines_add(o, "%s", row[i]);
        return;
    }
    if ((row = FIND(BRANCH_COND, mn))) {
        /* Branches poll interrupts before their 2nd cycle, and again before the
         * page fix-up cycle; a taken branch with no page cross ignores
         * interrupts that arrive during its last cycle (nesdev "CPU
         * interrupts", blargg cpu_interrupts_v2 5-branch_delays_irq). */
        lines_add(o, "uint8_t poll1 = c->poll;");
        lines_add(o, "int8_t off = (int8_t)@F;");
        lines_add(o, "if (%s) {", row[1]);
        lines_add(o, "    @R;");
        lines_add(o, "    uint16_t t = @P + off;");
        lines_add(o, "    if ((t ^ @P) & 0xFF00) rd(c, (@P & 0xFF00) | (t & 0xFF));");
        lines_add(o, "    else c->poll_prev = poll1;");
        lines_add(o, "    @B t;");
        lines_add(o, "}");
        return;
    }
    if (!strcmp(mn, "JMP")) {
        if (!strcmp(mode, "ABS")) {
            lines_add(o, "uint16_t lo = @F;");
            lines_add(o, "@J lo | @R << 8;");
        } else {
            lines_add(o, "uint16_t ptr = @F;");
            lines_add(o, "ptr |= @F << 8;");
            lines_add(o, "uint16_t lo = rd(c, ptr);");
            lines_add(o, "@J lo | rd(c, (ptr & 0xFF00) | ((ptr + 1) & 0xFF)) << 8;");  /* page-wrap bug */
        }
        return;
    }
    if (!strcmp(mode, "IMP")) {
        lines_add(o, "@R;");
        lines_add(o, "%s", FIND(IMPLIED, mn)[1]);
        return;
    }
    if (!strcmp(mode, "ACC")) {
        lines_add(o, "@R;");
        lines_add(o, "c->a = op_%s(c, c->a);", mn);
        return;
    }
    if (is(mn, READ)) {
        const char *fn = !strcmp(mn, "LAX") && !strcmp(mode, "IMM") ? "LXA" : mn;
        if (!strcmp(mode, "IMM")) {
            lines_add(o, "op_%s(c, @F);", fn);
            return;
        }
        ea(o, mode, 1);
        lines_add(o, "op_%s(c, rd(c, ea));", fn);
        return;
    }
    if ((row = FIND(STORE, mn))) {
        ea(o, mode, 0);
        lines_add(o, "wr(c, ea, %s);", row[1]);
        return;
    }
    if ((row = FIND(UNSTABLE, mn))) {
        /* The stored value is ANDed with (base high byte + 1), and on a page
         * cross that value also replaces the address high byte (nesdev "CPU
         * unofficial opcodes"; verified by SingleStepTests). */
        ea(o, mode, 0);
        if (!strcmp(mn, "TAS")) lines_add(o, "c->s = c->a & c->x;");
        lines_add(o, "uint8_t v = %s & (uint8_t)((base >> 8) + 1);", row[1]);
        lines_add(o, "if ((ea ^ base) & 0xFF00) ea = (ea & 0xFF) | (v << 8);");
        lines_add(o, "wr(c, ea, v);");
        return;
    }
    if (is(mn, RMW)) {
        ea(o, mode, 0);
        lines_add(o, "uint8_t v = rd(c, ea);");
        lines_add(o, "wr(c, ea, v);");
        lines_add(o, "v = op_%s(c, v);", mn);
        lines_add(o, "wr(c, ea, v);");
        return;
    }
    fprintf(stderr, "c6502: no C template for $%02X %s %s\n", op->code, mn, mode);
    exit(1);
}

void emit_cpu_switch(Buf *o) {
    bprintf(o, "/* generated by cpu6502_gen from cpu/6502/ops.c and cpu/6502/c6502.c; do not edit */\n");
    for (int i = 0; i < 256; i++) {
        Lines l;
        body(&OPS[i], &l);
        bprintf(o, "case 0x%02X: { /* %s %s */\n", i, OPS[i].mn, OPS[i].mode);
        for (int k = 0; k < l.n; k++)
            if (*l.s[k]) template_plain(o, "    ", l.s[k]);
        bprintf(o, "} break;\n");
    }
}
