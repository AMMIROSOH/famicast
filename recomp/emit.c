/* Emit C for a recompiled program (docs/02 "Recompiled code").
 *
 * One function per basic block. Each instruction is the interpreter's own
 * code for that opcode (c6502.c), so bus traffic and timing are identical by
 * construction; what the recompiler removes is fetch/decode/dispatch, and
 * with the address known, code bytes and PC values become constants
 * (static_body, M5b). Every instruction start is an entry point (the switch
 * at the top), so execution can resume mid-block, e.g. after an RTI. After
 * each instruction the block returns if an interrupt is due, so interrupts
 * are taken on the same instruction as in the interpreter (this is what
 * makes lockstep diffs exact). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "recomp.h"

void bprintf(Buf *b, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (b->len + (size_t)n + 1 > b->cap) {
        b->cap = (b->len + (size_t)n + 1) * 2;
        b->s = realloc(b->s, b->cap);
    }
    va_start(ap, fmt);
    vsnprintf(b->s + b->len, (size_t)n + 1, fmt, ap);
    va_end(ap);
    b->len += (size_t)n;
}

uint32_t fnv1a(const uint8_t *d, size_t n) {
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < n; i++) h = (h ^ d[i]) * 16777619u;
    return h;
}

static void disasm(char *out, size_t len, const Instr *i, const uint8_t *mem) {
    char raw[16] = "", arg[16] = "";
    for (int k = 0; k < i->size; k++)
        snprintf(raw + strlen(raw), sizeof raw - strlen(raw), k ? " %02X" : "%02X", mem[(uint16_t)(i->pc + k)]);
    const char *m = i->op->mode;
    unsigned v = i->operand;
    if (!strcmp(m, "ACC")) snprintf(arg, sizeof arg, "A");
    else if (!strcmp(m, "IMM")) snprintf(arg, sizeof arg, "#$%02X", v);
    else if (!strcmp(m, "ZP")) snprintf(arg, sizeof arg, "$%02X", v);
    else if (!strcmp(m, "ZPX")) snprintf(arg, sizeof arg, "$%02X,X", v);
    else if (!strcmp(m, "ZPY")) snprintf(arg, sizeof arg, "$%02X,Y", v);
    else if (!strcmp(m, "ABS")) snprintf(arg, sizeof arg, "$%04X", v);
    else if (!strcmp(m, "ABX")) snprintf(arg, sizeof arg, "$%04X,X", v);
    else if (!strcmp(m, "ABY")) snprintf(arg, sizeof arg, "$%04X,Y", v);
    else if (!strcmp(m, "IND")) snprintf(arg, sizeof arg, "($%04X)", v);
    else if (!strcmp(m, "IZX")) snprintf(arg, sizeof arg, "($%02X,X)", v);
    else if (!strcmp(m, "IZY")) snprintf(arg, sizeof arg, "($%02X),Y", v);
    else if (!strcmp(m, "REL")) snprintf(arg, sizeof arg, "$%04X", ins_target(i));
    snprintf(out, len, "%04X  %-8s  %s%s %s", i->pc, raw, i->op->official ? "" : "*", i->op->mn, arg);
    for (size_t n = strlen(out); n && out[n - 1] == ' '; n--) out[n - 1] = 0;
}

/* Rewrite one line for a known address: code fetches rd(c, c->pc[++])
 * become fetch_k() with the ROM byte as a constant, other reads of c->pc
 * (not assignments, not increments) become the constant PC. */
static void sub_pc(Buf *o, const char *s, uint16_t *pc, const uint8_t *mem) {
    while (*s) {
        if (!strncmp(s, "rd(c, c->pc++)", 14) || !strncmp(s, "rd(c, c->pc)", 12)) {
            int inc = s[11] == '+';
            bprintf(o, "fetch_k(c, 0x%04X, 0x%02X)", *pc, mem[*pc]);
            if (inc) (*pc)++;
            s += inc ? 14 : 12;
        } else if (!strncmp(s, "c->pc", 5) && strncmp(s + 5, " =", 2) && strncmp(s + 5, "++", 2)) {
            bprintf(o, "0x%04X", *pc);
            s += 5;
        } else {
            bprintf(o, "%c", *s++);
        }
    }
}

/* The interpreter's C for `ins` with constants, as above, up to the first
 * assignment of a run-time value to c->pc (RTS/RTI). c->pc is preset to the
 * next instruction; control transfers overwrite it. Writes indented lines. */
static void static_body(Buf *o, const Instr *ins, const uint8_t *mem) {
    Lines l;
    body(ins->op, &l);
    uint16_t pc = (uint16_t)(ins->pc + 1);
    int dynamic = 0;
    bprintf(o, "        c->pc = 0x%04X;\n", (uint16_t)(ins->pc + ins->size));
    for (int k = 0; k < l.n; k++) {
        const char *line = l.s[k];
        if (!*line) continue;
        bprintf(o, "        ");
        const char *as = strstr(line, "c->pc = ");
        int lhs_blank = as != NULL;
        for (const char *p = line; as && p < as; p++) lhs_blank &= *p == ' ';
        if (dynamic) {
            bprintf(o, "%s", line);
        } else if (lhs_blank) {  /* assignment: the right-hand side sees the old value */
            bprintf(o, "%.*s", (int)(as - line) + 8, line);
            sub_pc(o, as + 8, &pc, mem);
            dynamic = as == line;  /* top level (RTS/RTI); an indented one is a taken branch */
        } else {
            sub_pc(o, line, &pc, mem);
        }
        bprintf(o, "\n");
    }
}

/* Could this instruction write to $8000-$FFFF (a mapper register)? */
static int may_write_rom(const Instr *i) {
    static const char STORES[] = " STA STX STY SAX AHX SHX SHY TAS ASL LSR ROL ROR INC DEC SLO RLA SRE RRA DCP ISB ";
    char key[6];
    snprintf(key, sizeof key, " %s ", i->op->mn);
    const char *m = i->op->mode;
    if (!strstr(STORES, key) || !strcmp(m, "IMP") || !strcmp(m, "ACC") || m[0] == 'Z') return 0;
    if (!strcmp(m, "ABS")) return i->operand >= 0x8000;
    if (!strcmp(m, "ABX") || !strcmp(m, "ABY")) return i->operand + 0xFF >= 0x8000;
    return 1;  /* (zp,X), (zp),Y: unknown address */
}

/* Instructions of the block starting at `start`, in execution order. */
static int block_instrs(const Program *p, const AddrSet *starts, const uint8_t *mem, uint16_t start, Instr *out) {
    int n = 0;
    for (uint16_t pc = start;;) {
        Instr ins = decode_at(mem, pc);
        out[n++] = ins;
        uint16_t nxt = (uint16_t)(pc + ins.size);
        if (is_transfer(&ins) || starts->b[nxt] || !p->instrs.b[nxt]) return n;
        pc = nxt;
    }
}

static void c_string(Buf *o, const char *s) {
    bprintf(o, "\"");
    for (; *s; s++) bprintf(o, (*s == '"' || *s == '\\') ? "\\%c" : "%c", *s);
    bprintf(o, "\"");
}

void emit_program(Buf *o, Region *regions, int nregions, const char *rom_name, uint32_t prg_hash,
                  int nbanks, int banked, const uint8_t *rom, size_t rom_size) {
    static AddrSet starts[1];
    static Instr ins[0x10000];
    long nblocks = 0, ninstrs = 0;
    for (int r = 0; r < nregions; r++) {
        block_starts(regions[r].prog, regions[r].mem, starts);
        for (uint32_t a = 0; a < 0x10000; a++) {
            nblocks += starts->b[a];
            ninstrs += regions[r].prog->instrs.b[a];
        }
    }
    bprintf(o, "/* Generated by nesrecomp from %s. Do not edit.\n", rom_name);
    bprintf(o, " * %ld blocks, %ld instructions. PRG hash %08X. */\n", nblocks, ninstrs, prg_hash);
    bprintf(o, "#include \"cpu_ops.h\"\n#include \"recomp_rt.h\"\n\n");

    /* tables[bank * 4 + window][offset] = (block's bank << 16) | block start */
    uint32_t **tables = calloc((size_t)nbanks * 4, sizeof *tables);
    for (int r = 0; r < nregions; r++) {
        const Region *R = &regions[r];
        int w0 = 0;
        while (R->banks[w0] < 0) w0++;  /* first window of the unit: its page changes on any switch */
        block_starts(R->prog, R->mem, starts);
        long covered = 0;
        for (uint32_t start = 0x8000; start < 0x10000; start++) {
            if (!starts->b[start]) continue;
            int n = block_instrs(R->prog, starts, R->mem, (uint16_t)start, ins);
            covered += n;
            int bank = R->banks[window_of(start)];
            char name[16];
            snprintf(name, sizeof name, "b_%02X_%04X", bank, start);
            bprintf(o, "static void %s(Cpu *c) {\n", name);
            int stores = 0;
            for (int k = 0; k < n - 1; k++) stores |= may_write_rom(&ins[k]);
            if (banked && stores)
                bprintf(o, "    const uint8_t *const page = c->read_map[%d];  /* bank %d at $%04X */\n",
                        16 + w0 * 4, R->banks[w0], 0x8000 + w0 * 0x2000);
            bprintf(o, "    switch (c->pc) {\n");
            for (int k = 0; k < n; k++) bprintf(o, "    case 0x%04X: goto i_%04X;\n", ins[k].pc, ins[k].pc);
            bprintf(o, "    default: return;\n    }\n");
            for (int k = 0; k < n; k++) {
                const Instr *i = &ins[k];
                char text[64];
                disasm(text, sizeof text, i, R->mem);
                bprintf(o, "i_%04X: /* %s */\n", i->pc, text);
                bprintf(o, "    fetch_k(c, 0x%04X, 0x%02X);\n    {\n", i->pc, R->mem[i->pc]);
                static_body(o, i, R->mem);
                bprintf(o, "    }\n");
                int last = k == n - 1;
                if (!strcmp(i->op->mn, "KIL")) bprintf(o, "    return;\n");
                else if (banked && may_write_rom(i) && !last)
                    bprintf(o, "    if (c->poll_prev || c->read_map[%d] != page) return;\n", 16 + w0 * 4);
                else if (!last) bprintf(o, "    if (c->poll_prev) return;\n");
                int iw = window_of(i->pc), key = R->banks[iw] * 4 + iw;
                if (!tables[key]) tables[key] = calloc(0x2000, sizeof **tables);
                tables[key][i->pc & 0x1FFF] = (uint32_t)bank << 16 | start;
            }
            bprintf(o, "}\n\n");
        }
        long decoded = 0;
        for (uint32_t a = 0; a < 0x10000; a++) decoded += R->prog->instrs.b[a];
        if (covered != decoded) {
            fprintf(stderr, "nesrecomp: %ld instructions outside any block\n", decoded - covered);
            exit(1);
        }
    }
    for (int key = 0; key < nbanks * 4; key++) {
        if (!tables[key]) continue;
        bprintf(o, "static const RecompFn r_%02X_%d[0x2000] = {\n", key / 4, key % 4);
        for (int off = 0; off < 0x2000; off++)
            if (tables[key][off])
                bprintf(o, "    [0x%04X] = b_%02X_%04X,\n", off, tables[key][off] >> 16, tables[key][off] & 0xFFFF);
        bprintf(o, "};\n\n");
    }
    bprintf(o, "static const RecompFn *const regions[%d][4] = {\n", nbanks);
    for (int key = 0; key < nbanks * 4; key++)
        if (tables[key]) bprintf(o, "    [%d][%d] = r_%02X_%d,\n", key / 4, key % 4, key / 4, key % 4);
    bprintf(o, "};\n\n");
    bprintf(o, "static RecompFn lookup(uint16_t pc, uint32_t bank) {\n");
    bprintf(o, "    if (pc < 0x8000 || bank >= %d) return 0;\n", nbanks);
    bprintf(o, "    const RecompFn *r = regions[bank][(pc >> 13) & 3];\n");
    bprintf(o, "    return r ? r[pc & 0x1FFF] : 0;\n}\n\n");
    /* The whole iNES file: data is read through pointers the analysis cannot
     * bound, so no part of the ROM can be left out safely. NULL: not embedded. */
    if (rom) {
        bprintf(o, "static const uint8_t rom_image[%zu] = {", rom_size);
        for (size_t i = 0; i < rom_size; i++) bprintf(o, i % 16 ? "0x%02X," : "\n    0x%02X,", rom[i]);
        bprintf(o, "\n};\n\n");
    }
    bprintf(o, "static const RecompProgram program = {\n    0x%08Xu, ", prg_hash);
    c_string(o, rom_name);
    bprintf(o, rom ? ", lookup, rom_image, sizeof rom_image,\n};\n" : ", lookup, 0, 0,\n};\n");
    bprintf(o, "const RecompProgram *const recomp_program = &program;\n");
    for (int key = 0; key < nbanks * 4; key++) free(tables[key]);
    free(tables);
}

void emit_cpu_switch(Buf *o) {
    bprintf(o, "/* generated by nesrecomp --cpu-switch from recomp/ops.c and recomp/c6502.c; do not edit */\n");
    for (int i = 0; i < 256; i++) {
        Lines l;
        body(&OPS[i], &l);
        bprintf(o, "case 0x%02X: { /* %s %s */\n", i, OPS[i].mn, OPS[i].mode);
        for (int k = 0; k < l.n; k++)
            if (*l.s[k]) bprintf(o, "    %s\n", l.s[k]);
        bprintf(o, "} break;\n");
    }
}
