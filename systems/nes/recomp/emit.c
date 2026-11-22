/* Emit C for a recompiled program (docs/architecture.md "Recompiled code").
 *
 * One function per basic block. Each instruction is the interpreter's own
 * code for that opcode (c6502.c), so bus traffic and timing are identical by
 * construction; what the recompiler removes is fetch/decode/dispatch, and
 * with the address known, code bytes and PC values become constants
 * (template_expand, M5b). Every instruction start is an entry point (the switch
 * at the top), so execution can resume mid-block, e.g. after an RTI. After
 * each instruction the block returns if an interrupt is due, so interrupts
 * are taken on the same instruction as in the interpreter (this is what
 * makes lockstep diffs exact). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "recomp.h"


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

/* The interpreter's C for `ins` with constants, as above, up to the first
 * jump to a run-time value (RTS/RTI: @J; the right-hand side still sees the
 * known PC). c->pc is preset to the next instruction; control transfers
 * overwrite it. Writes indented lines. */
static void static_body(Buf *o, const Instr *ins, const uint8_t *mem) {
    Lines l;
    body(ins->op, &l);
    uint32_t pc = (uint16_t)(ins->pc + 1);
    int dynamic = 0;
    unsigned fetched = 0;
    bprintf(o, "        c->pc = 0x%04X;\n", (uint16_t)(ins->pc + ins->size));
    for (int k = 0; k < l.n; k++) {
        if (!*l.s[k]) continue;
        if (dynamic) { template_plain(o, "        ", l.s[k]); continue; }
        bprintf(o, "        ");
        template_expand(o, l.s[k], &pc, mem, 0xFFFF, ins->pc, &fetched, &dynamic);
        bprintf(o, "\n");
    }
    /* Every operand byte (offsets 1 to size-1) must have been fetched, and
     * nothing past the next instruction's first byte (implied instructions
     * read it as a dummy). If a template changes that, stop the build
     * rather than emit code that reads the wrong bytes. */
    unsigned need = ((1u << ins->size) - 1) & ~1u, allowed = (1u << (ins->size + 1)) - 2;
    if ((fetched & need) != need || (fetched & ~allowed)) {
        fprintf(stderr, "nesrecomp: $%04X %s: the template fetches offsets %X (bits), the instruction"
                        " has %d bytes (cpu/6502/c6502.c)\n",
                ins->pc, ins->op->mn, fetched, ins->size);
        exit(2);
    }
}

/* An instruction whose operand is in the next window: the interpreter's C
 * as is (operand read at run time), only the opcode is known. */
static void dynamic_body(Buf *o, const Instr *ins) {
    Lines l;
    body(ins->op, &l);
    bprintf(o, "        c->pc = 0x%04X;  /* operand in the next window: read at run time */\n",
            (uint16_t)(ins->pc + 1));
    for (int k = 0; k < l.n; k++)
        if (*l.s[k]) template_plain(o, "        ", l.s[k]);
}

static int is_store(const Instr *i) {
    static const char STORES[] = " STA STX STY SAX AHX SHX SHY TAS ASL LSR ROL ROR INC DEC SLO RLA SRE RRA DCP ISB ";
    char key[6];
    snprintf(key, sizeof key, " %s ", i->op->mn);
    return strstr(STORES, key) && strcmp(i->op->mode, "IMP") && strcmp(i->op->mode, "ACC");
}

/* Could this instruction write to a PRG bank register (at bank_reg and up;
 * $6000-$7FFF is PRG RAM unless the registers start there: Bandai FCG-1/2)? */
static int may_write_rom(const Instr *i, int bank_reg) {
    const char *m = i->op->mode;
    if (!is_store(i) || m[0] == 'Z') return 0;
    if (!strcmp(m, "ABS"))
        return i->operand >= 0x8000 || (i->operand >= bank_reg && (i->operand < 0x6000 || bank_reg == 0x6000));
    if (!strcmp(m, "ABX") || !strcmp(m, "ABY")) return i->operand + 0xFF >= bank_reg;
    return 1;  /* (zp,X), (zp),Y: unknown address */
}

/* Could this instruction write to RAM at [lo, hi) (code of a disk file;
 * $0000-$07FF is mirrored up to $1FFF)? Stack writes never reach it: disk
 * files from $0200 only (main.c). */
static int may_write_ram(const Instr *i, uint32_t lo, uint32_t hi) {
    const char *m = i->op->mode;
    uint32_t a0 = i->operand, a1 = a0;
    if (!is_store(i)) return 0;
    if (!strcmp(m, "ZPX") || !strcmp(m, "ZPY")) a0 = 0, a1 = 0xFF;
    else if (!strcmp(m, "ABX") || !strcmp(m, "ABY")) a1 = a0 + 0xFF;
    else if (strcmp(m, "ZP") && strcmp(m, "ABS")) return 1;  /* indirect: unknown address */
    for (uint32_t x = a0; x <= a1; x++) {
        uint32_t y = x < 0x2000 ? x & 0x7FF : x & 0xFFFF;
        if (y >= lo && y < hi) return 1;
    }
    return 0;
}

static void block_name(char *out, size_t len, const Region *R, uint32_t a) {
    if (R->ram) snprintf(out, len, "f%d_%04X", R->file, a);
    else snprintf(out, len, "b_%02X_%04X", R->banks[window_of(a)], a);
}

static int cmp_entry(const void *a, const void *b) {
    const uint32_t *x = a, *y = b;  /* {pc, block} */
    return x[0] != y[0] ? (x[0] > y[0]) - (x[0] < y[0]) : (x[1] > y[1]) - (x[1] < y[1]);
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
                  int nbanks, int bank_reg, const uint8_t *rom, size_t rom_size, uint16_t dispatch_at,
                  const uint8_t *bios) {
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
    bprintf(o, "#include \"cpu6502_ops.h\"\n#include \"recomp_rt.h\"\n\n");
    char name[16], next_name[16];
    for (int r = 0; r < nregions; r++) {  /* prototypes: blocks chain to each other */
        block_starts(regions[r].prog, regions[r].mem, starts);
        for (uint32_t a = 0; a < 0x10000; a++)
            if (starts->b[a]) block_name(name, sizeof name, &regions[r], a), bprintf(o, "static void %s(Cpu *c);\n", name);
    }
    bprintf(o, "\n");

    /* tables[bank * 4 + window][offset] = (block's bank << 16) | block start */
    uint32_t **tables = calloc((size_t)nbanks * 4, sizeof *tables);
    /* disk files: the blocks, and {pc, block} for each of their instructions */
    Buf ram_blocks = {0};
    uint32_t (*entries)[2] = NULL;
    size_t nentries = 0, cap = 0, nram_blocks = 0;
    for (int r = 0; r < nregions; r++) {
        const Region *R = &regions[r];
        const int banked = bank_reg != 0 && !R->ram;
        int w0 = 0;
        while (!R->ram && R->banks[w0] < 0) w0++;  /* first window of the unit: its page changes on any switch */
        block_starts(R->prog, R->mem, starts);
        long covered = 0;
        for (uint32_t start = 0; start < 0x10000; start++) {
            if (!starts->b[start]) continue;
            int n = block_instrs(R->prog, starts, R->mem, (uint16_t)start, ins);
            covered += n;
            int bank = R->ram ? 0 : R->banks[window_of(start)];
            block_name(name, sizeof name, R, start);
            /* RAM blocks: their bytes up to the file's end (a straddling
             * operand is read at run time) */
            uint32_t past = ins[n - 1].pc + ins[n - 1].size, len = (R->ram && past > R->hi ? R->hi : past) - start;
            char check[96] = "";
            if (R->ram) {
                bprintf(o, "static const uint8_t k_%s[%u] = {", name, len);
                for (uint32_t a = start; a < start + len; a++) bprintf(o, a > start ? ",0x%02X" : "0x%02X", R->mem[a]);
                bprintf(o, "};\n");
                snprintf(check, sizeof check, "!recomp_ram_is(c, 0x%04X, k_%s, %u)", start, name, len);
                bprintf(&ram_blocks, "    {%s, k_%s, 0x%04X, %u},\n", name, name, start, len);
                for (int k = 0; k < n; k++) {
                    if (nentries == cap) entries = realloc(entries, (cap = cap * 2 + 1024) * sizeof *entries);
                    entries[nentries][0] = ins[k].pc, entries[nentries++][1] = (uint32_t)nram_blocks;
                }
                nram_blocks++;
            }
            bprintf(o, "static void %s(Cpu *c) {\n", name);
            if (R->ram) bprintf(o, "    if (%s) return;  /* other code loaded here */\n", check);
            int stores = 0;
            for (int k = 0; k < n - 1; k++) stores |= may_write_rom(&ins[k], bank_reg);
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
                if (R->prog->straddle.b[i->pc]) dynamic_body(o, i);
                else static_body(o, i, R->mem);
                bprintf(o, "    }\n");
                int last = k == n - 1;
                if (!strcmp(i->op->mn, "KIL")) bprintf(o, "    return;\n");
                else if (banked && may_write_rom(i, bank_reg) && !last)
                    bprintf(o, "    if (c->poll_prev || c->read_map[%d] != page) return;\n", 16 + w0 * 4);
                else if (R->ram && may_write_ram(i, start, start + len) && !last)
                    bprintf(o, "    if (c->poll_prev || %s) return;  /* code changed */\n", check);
                else if (!last) bprintf(o, "    if (c->poll_prev) return;\n");
                if (R->ram) continue;
                int iw = window_of(i->pc), key = R->banks[iw] * 4 + iw;
                if (!tables[key]) tables[key] = calloc(0x2000, sizeof **tables);
                tables[key][i->pc & 0x1FFF] = (uint32_t)bank << 16 | start;
            }
            /* continue in the next block if it is known here: a static target or
             * fall-through, in this region (so in the bank checked above), and
             * no mapper store that could have switched it */
            const Instr *end = &ins[n - 1];
            int next[2], nnext = 0, t = ins_target(end);
            uint16_t after = (uint16_t)(end->pc + end->size);
            if (is_branch(end->op->mn)) next[nnext++] = t, next[nnext++] = after;
            else if (t >= 0) next[nnext++] = t;                       /* JMP abs, JSR */
            else if (!is_transfer(end)) next[nnext++] = after;        /* the next block starts here */
            if (nnext == 2 && next[0] == next[1]) nnext = 1;
            if ((banked && may_write_rom(end, bank_reg)) || R->prog->straddle.b[end->pc]) nnext = 0;
            int chained = 0;
            for (int k = 0; k < nnext; k++) {
                if (!R->prog->instrs.b[next[k]] || !starts->b[next[k]] || (dispatch_at && next[k] == dispatch_at)) continue;
                if (!chained++) bprintf(o, "    if (c->poll_prev | c->yield) return;\n    switch (c->pc) {\n");
                block_name(next_name, sizeof next_name, R, (uint32_t)next[k]);
                bprintf(o, "    case 0x%04X: RECOMP_CHAIN(%s);\n", next[k], next_name);
            }
            if (chained) bprintf(o, "    }\n");
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
    if (bios) {
        bprintf(o, "static const uint8_t bios_image[0x2000] = {");
        for (size_t i = 0; i < 0x2000; i++) bprintf(o, i % 16 ? "0x%02X," : "\n    0x%02X,", bios[i]);
        bprintf(o, "\n};\n\n");
    }
    if (nram_blocks) {
        bprintf(o, "static const RecompRamBlock ram_blocks[] = {\n%s};\n\n", ram_blocks.s);
        qsort(entries, nentries, sizeof *entries, cmp_entry);
        bprintf(o, "static const RecompRamEntry ram_entries[] = {\n");
        for (size_t i = 0; i < nentries; i++) bprintf(o, "    {0x%04X, %u},\n", entries[i][0], entries[i][1]);
        bprintf(o, "};\n\n");
    }
    bprintf(o, "static const RecompProgram program = {\n    .prg_hash = 0x%08Xu,\n    .rom_name = ", prg_hash);
    c_string(o, rom_name);
    bprintf(o, ",\n    .lookup = lookup,\n");
    if (rom) bprintf(o, "    .rom = rom_image,\n    .rom_size = sizeof rom_image,\n");
    if (bios) bprintf(o, "    .bios = bios_image,\n");
    if (nram_blocks)
        bprintf(o, "    .ram_blocks = ram_blocks,\n    .ram_entries = ram_entries,\n"
                   "    .ram_entries_n = sizeof ram_entries / sizeof *ram_entries,\n");
    bprintf(o, "};\nconst RecompProgram *const recomp_program = &program;\n");
    for (int key = 0; key < nbanks * 4; key++) free(tables[key]);
    free(tables);
    free(ram_blocks.s);
    free(entries);
}
