/* mdrecomp: the Genesis 68000 recompiler (build-time tool).
 *   mdrecomp ROM -o OUT.c [--misses TRACE]... [--no-rom]
 *       TRACE: genesis_headless --misses output or a program's NAME.misses
 *       ("MISS $PC" lines: entries); --no-rom: do not embed the ROM.
 *
 * The code is the cartridge ROM that is always mapped (md_code_end: not
 * the SSF2 banks, not where battery RAM can be switched in), one region,
 * so no bank lookup. Entries: the reset and exception vectors and the
 * trace's addresses. Static flow is followed: Bcc, BRA, BSR, DBcc, and JMP
 * and JSR to absolute or PC-relative targets; the rest (jump tables,
 * returns) is resolved at run time by the dispatcher.
 *
 * One C function per basic block. Each instruction is the interpreter's
 * own handler body (the template, cpu/m68k/m68k.c, as the interpreter
 * expands it) with `op` a constant, so the compiler folds the register
 * fields and bus traffic and timing are the interpreter's by construction.
 * A block ends after any instruction whose template can change the PC
 * (branches, jumps, traps, conditional exceptions, privileged and SR
 * writes). After each instruction the Z80 catches up and the block
 * returns if the interpreter's loop would do something else next
 * (recomp_rt.h MD_STOP). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "m68k.h"
#include "md.h"

static Md md;
static uint32_t end;                      /* md_code_end */
static uint8_t *entries, *instr, *starts;  /* a byte per address below end */

static uint16_t word(uint32_t a) { return (uint16_t)(md.rom[a] << 8 | md.rom[a + 1]); }

typedef struct {
    uint32_t pc;
    uint16_t op;
    uint8_t len;
    uint8_t flow;  /* the template can change the PC: the block ends here */
} Ins;

static int decode(uint32_t pc, Ins *i) {
    if (pc & 1 || pc + 2 > end) return 0;
    i->pc = pc, i->op = word(pc), i->len = (uint8_t)(2 + 2 * m68k_ext_words(i->op));
    if (pc + i->len > end) return 0;
    Lines l;
    m68k_body(i->op, &l);
    i->flow = 0;
    for (int k = 0; k < l.n; k++) i->flow |= strstr(l.s[k], "@J") || strstr(l.s[k], "@B");
    return 1;
}

/* Can the next instruction run after this one (BSR and JSR: on return)? */
static int falls_through(const Ins *i) {
    uint16_t op = i->op;
    Lines l;
    if (!m68k_body(op, &l)) return 0;  /* illegal, line A, line F */
    return (op & 0xFF00) != 0x6000 && (op & 0xFFC0) != 0x4EC0 && op != 0x4E73 && op != 0x4E75 && op != 0x4E77 &&
           (op & 0xFFF0) != 0x4E40;  /* BRA JMP RTE RTS RTR TRAP */
}

/* The static target of a branch, DBcc, JMP or JSR; -1 if none (or not code). */
static long target(const Ins *i) {
    uint16_t op = i->op;
    uint32_t t;
    if ((op & 0xF000) == 0x6000) t = i->pc + 2 + (op & 0xFF ? (uint32_t)(int8_t)op : (uint32_t)(int16_t)word(i->pc + 2));
    else if ((op & 0xF0F8) == 0x50C8) t = i->pc + 2 + (uint32_t)(int16_t)word(i->pc + 2);
    else if ((op & 0xFF80) == 0x4E80 && op & 0x3F) {
        if ((op & 0x3F) == 0x38) t = (uint32_t)(int16_t)word(i->pc + 2);
        else if ((op & 0x3F) == 0x39) t = (uint32_t)word(i->pc + 2) << 16 | word(i->pc + 4);
        else if ((op & 0x3F) == 0x3A) t = i->pc + 2 + (uint32_t)(int16_t)word(i->pc + 2);
        else return -1;
    } else return -1;
    t &= 0xFFFFFF;
    return t & 1 || t >= end ? -1 : (long)t;
}

static void explore(void) {
    uint32_t *work = malloc(sizeof *work * (end / 2 + 1));
    size_t n = 0;
    for (uint32_t a = 0; a < end; a += 2)
        if (entries[a]) work[n++] = a;
    while (n) {
        uint32_t pc = work[--n];
        Ins i;
        if (instr[pc] || !decode(pc, &i)) continue;
        instr[pc] = 1;
        long t = target(&i);
        if (t >= 0 && !instr[t]) work[n++] = (uint32_t)t;
        if (falls_through(&i) && pc + i.len < end && !instr[pc + i.len]) work[n++] = pc + i.len;
    }
    free(work);
}

/* Block starts: static targets, what follows a block's last instruction,
 * and entries no other instruction falls into. */
static void block_starts(void) {
    uint8_t *fallen_into = calloc(end + 8, 1);
    for (uint32_t a = 0; a < end; a += 2) {
        Ins i;
        if (!instr[a] || !decode(a, &i)) continue;
        long t = target(&i);
        if (t >= 0 && instr[t]) starts[t] = 1;
        uint32_t next = a + i.len;
        if (!falls_through(&i) || next >= end || !instr[next]) continue;
        if (i.flow) starts[next] = 1;
        else fallen_into[next] = 1;
    }
    for (uint32_t a = 0; a < end; a += 2)
        if (instr[a] && entries[a] && !fallen_into[a]) starts[a] = 1;
    free(fallen_into);
}

static int block_instrs(uint32_t start, Ins *out) {
    int n = 0;
    for (uint32_t pc = start;;) {
        decode(pc, &out[n]);
        const Ins *i = &out[n++];
        uint32_t next = pc + i->len;
        if (i->flow || !falls_through(i) || next >= end || !instr[next] || starts[next]) return n;
        pc = next;
    }
}

static void emit_block(Buf *o, uint32_t start, const Ins *ins, int n) {
    bprintf(o, "static void b_%06X(M68k *c) {\n    Md *const m = c->bus;\n    switch ((c->pc - 4) & 0xFFFFFF) {\n", start);
    for (int k = 0; k < n; k++) bprintf(o, "    case 0x%06X: goto i_%06X;\n", ins[k].pc, ins[k].pc);
    bprintf(o, "    default: return;\n    }\n");
    for (int k = 0; k < n; k++) {
        const Ins *i = &ins[k];
        bprintf(o, "i_%06X: /*", i->pc);
        for (int w = 0; w < i->len; w += 2) bprintf(o, " %04X", word(i->pc + (uint32_t)w));
        bprintf(o, " */\n    c->ae_adj = -2;\n    c->trace = (c->sr & M68K_T) != 0;\n    c->op = 0x%04X;\n", i->op);
        bprintf(o, "    {\n        const uint16_t op = 0x%04X;\n        (void)op;\n", i->op);
        Lines l;
        m68k_body(i->op, &l);
        for (int k2 = 0; k2 < l.n; k2++) template_plain(o, "        ", l.s[k2]);
        bprintf(o, "    }\n    md_z80_catch_up(m);\n    if (MD_STOP(m, c)) return;\n");
    }
    /* chaining: to a block known from here */
    const Ins *last = &ins[n - 1];
    long next[2] = {target(last), falls_through(last) ? (long)(last->pc + last->len) : -1};
    int chained = 0;
    for (int k = 0; k < 2; k++) {
        if (next[k] < 0 || (uint32_t)next[k] >= end || !starts[next[k]] || (k == 1 && next[1] == next[0])) continue;
        if (!chained++) bprintf(o, "    switch ((c->pc - 4) & 0xFFFFFF) {\n");
        bprintf(o, "    case 0x%06lX: RECOMP_CHAIN(b_%06lX);\n", next[k], next[k]);
    }
    if (chained) bprintf(o, "    }\n");
    bprintf(o, "}\n\n");
}

static uint32_t fnv1a(const uint8_t *d, size_t n) {
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < n; i++) h = (h ^ d[i]) * 16777619u;
    return h;
}

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: mdrecomp ROM -o OUT.c [--misses TRACE]... [--no-rom]\n"); return 2; }
    const char *rom_path = argv[1], *out = NULL;
    int embed = 1;
    for (int i = 2; i < argc; i++)
        if (!strcmp(argv[i], "-o") && i + 1 < argc) out = argv[++i];
        else if (!strcmp(argv[i], "--no-rom")) embed = 0;
        else if (!strcmp(argv[i], "--misses") && i + 1 < argc) i++;  /* read below, once the ROM is in */
        else { fprintf(stderr, "mdrecomp: unknown option %s\n", argv[i]); return 2; }
    if (!out) { fprintf(stderr, "mdrecomp: -o OUT.c is needed\n"); return 2; }
    FILE *f = fopen(rom_path, "rb");
    if (!f) { perror(rom_path); return 2; }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *data = size > 0 ? malloc((size_t)size) : NULL;
    if (!data || fread(data, 1, (size_t)size, f) != (size_t)size) { perror(rom_path); return 2; }
    fclose(f);
    char err[128];
    if (md_load(&md, data, (size_t)size, err, sizeof err)) { fprintf(stderr, "%s: %s\n", rom_path, err); return 2; }
    free(data);
    end = md_code_end(&md);
    entries = calloc(end + 8, 1), instr = calloc(end + 8, 1), starts = calloc(end + 8, 1);
    if (!entries || !instr || !starts) { fprintf(stderr, "mdrecomp: out of memory\n"); return 2; }
    for (uint32_t v = 1; v < 64; v++) {  /* the reset PC and the exception vectors */
        uint32_t a = ((uint32_t)word(v * 4) << 16 | word(v * 4 + 2)) & 0xFFFFFF;
        if (!(a & 1) && a < end) entries[a] = 1;
    }
    long misses = 0;
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--misses")) continue;
        FILE *m = fopen(argv[++i], "r");
        if (!m) { perror(argv[i]); return 2; }
        char line[128];
        unsigned pc;
        while (fgets(line, sizeof line, m))
            if (sscanf(line, "MISS $%x", &pc) == 1 && !(pc & 1) && pc < end) entries[pc] = 1, misses++;
        fclose(m);
    }
    explore();
    block_starts();
    Buf o = {0};
    const char *name = rom_path;
    for (const char *p = rom_path; *p; p++)
        if (*p == '/' || *p == '\\') name = p + 1;
    long nblocks = 0, ninstr = 0;
    for (uint32_t a = 0; a < end; a += 2) nblocks += starts[a], ninstr += instr[a];
    bprintf(&o, "/* Generated by mdrecomp from %s. Do not edit.\n * %ld blocks, %ld instructions. */\n", name, nblocks, ninstr);
    bprintf(&o, "#include \"m68k_ops.h\"\n#include \"recomp_rt.h\"\n\n");
    for (uint32_t a = 0; a < end; a += 2)  /* prototypes: blocks chain to each other */
        if (starts[a]) bprintf(&o, "static void b_%06X(M68k *c);\n", a);
    bprintf(&o, "\n");
    Ins *ins = malloc(sizeof *ins * (end / 2 + 1));
    uint32_t *block_of = calloc(end / 2 + 1, sizeof *block_of);  /* instruction -> block start + 1 */
    for (uint32_t s = 0; s < end; s += 2) {
        if (!starts[s]) continue;
        int n = block_instrs(s, ins);
        emit_block(&o, s, ins, n);
        for (int k = 0; k < n; k++)
            if (!block_of[ins[k].pc >> 1]) block_of[ins[k].pc >> 1] = s + 1;
    }
    for (uint32_t a = 0; a < end; a += 2)
        if (instr[a] && !block_of[a >> 1]) { fprintf(stderr, "mdrecomp: $%06X is in no block\n", a); return 1; }
    /* lookup: the instructions in address order, binary searched */
    bprintf(&o, "static const uint32_t ins_at[%ld] = {", ninstr ? ninstr : 1);
    long k = 0;
    for (uint32_t a = 0; a < end; a += 2)
        if (instr[a]) bprintf(&o, k++ % 8 ? "0x%06X," : "\n    0x%06X,", a);
    bprintf(&o, "%s\n};\nstatic const MdRecompFn ins_block[%ld] = {", ninstr ? "" : "0", ninstr ? ninstr : 1);
    k = 0;
    for (uint32_t a = 0; a < end; a += 2)
        if (instr[a]) bprintf(&o, k++ % 4 ? "b_%06X," : "\n    b_%06X,", block_of[a >> 1] - 1);
    bprintf(&o, "%s\n};\n\n", ninstr ? "" : "0");
    bprintf(&o, "static MdRecompFn lookup(uint32_t at) {\n    size_t lo = 0, hi = %ld;\n", ninstr);
    bprintf(&o, "    while (lo < hi) {\n        size_t mid = (lo + hi) / 2;\n");
    bprintf(&o, "        if (ins_at[mid] < at) lo = mid + 1;\n        else hi = mid;\n    }\n");
    bprintf(&o, "    return lo < %ld && ins_at[lo] == at ? ins_block[lo] : 0;\n}\n\n", ninstr);
    if (embed) {
        bprintf(&o, "static const uint8_t rom_image[%u] = {", md.rom_size);
        for (uint32_t i = 0; i < md.rom_size; i++) bprintf(&o, i % 16 ? "0x%02X," : "\n    0x%02X,", md.rom[i]);
        bprintf(&o, "\n};\n\n");
    }
    bprintf(&o, "static const MdRecompProgram program = {\n    .rom_hash = 0x%08Xu,\n    .rom_name = \"", fnv1a(md.rom, md.rom_size));
    for (const char *s = name; *s; s++) bprintf(&o, (*s == '"' || *s == '\\') ? "\\%c" : "%c", *s);
    bprintf(&o, "\",\n    .lookup = lookup,\n");
    if (embed) bprintf(&o, "    .rom = rom_image,\n    .rom_size = sizeof rom_image,\n");
    bprintf(&o, "};\nconst MdRecompProgram *const md_recomp_program = &program;\n");
    buf_write_file(out, &o);
    printf("mdrecomp: %ld blocks, %ld instructions (%ld trace entries) -> %s\n", nblocks, ninstr, misses, out);
    md_free(&md);
    return 0;
}
