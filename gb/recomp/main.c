/* gbrecomp: the Game Boy recompiler (build-time tool).
 *   gbrecomp ROM -o OUT.c [--misses TRACE]... [--no-rom]
 *                                   recompile ROM; TRACE: gb_headless --misses output
 *                                   (each "MISS $PC bank B" an entry); --no-rom: do not
 *                                   embed the ROM (the program then needs the file)
 *   gbrecomp --cpu-switch OUT.inc   the interpreter's opcode switch (gb/runtime/cpu.c)
 *   gbrecomp --ops                  every opcode: "CB? XX size name" (tests) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cart.h"
#include "recomp.h"

static Region *region(Region **regions, const GbCart *cart, int bank, int window) {
    Region **r = &regions[bank * 2 + window];
    if (!*r) {
        if (!(*r = calloc(1, sizeof **r))) { fprintf(stderr, "gbrecomp: out of memory\n"); exit(2); }
        (*r)->bank = bank, (*r)->window = window;
        memcpy((*r)->mem + window * 0x4000, cart->rom + (size_t)bank * 0x4000, 0x4000);
    }
    return *r;
}

static int recompile(int argc, char **argv) {
    const char *rom_path = argv[1], *out = NULL;
    int embed = 1;
    for (int i = 2; i < argc; i++)
        if (!strcmp(argv[i], "-o") && i + 1 < argc) out = argv[++i];
        else if (!strcmp(argv[i], "--no-rom")) embed = 0;
        else if (!strcmp(argv[i], "--misses") && i + 1 < argc) i++;  /* read below, once the ROM is in */
        else { fprintf(stderr, "gbrecomp: unknown option %s\n", argv[i]); return 2; }
    if (!out) { fprintf(stderr, "gbrecomp: -o OUT.c is needed\n"); return 2; }
    FILE *f = fopen(rom_path, "rb");
    if (!f) { perror(rom_path); return 2; }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *data = malloc((size_t)size);
    if (!data || fread(data, 1, (size_t)size, f) != (size_t)size) { perror(rom_path); return 2; }
    fclose(f);
    static GbCart cart;  /* the ROM as the runtime loads it (padded to a power of two) */
    char err[128];
    if (gb_cart_load(&cart, data, (size_t)size, err, sizeof err) != 0) { fprintf(stderr, "%s: %s\n", rom_path, err); return 2; }
    free(data);
    int nbanks = (int)(cart.rom_size / 0x4000);
    Region **regions = calloc((size_t)nbanks * 2, sizeof *regions);
    /* bank 0: the reset vector, RST and interrupt vectors */
    Region *r0 = region(regions, &cart, 0, 0);
    r0->entries[0x100] = 1;
    for (int v = 0; v <= 0x60; v += 8) r0->entries[v] = 1;
    long misses = 0;
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--misses")) continue;
        FILE *m = fopen(argv[++i], "r");
        if (!m) { perror(argv[i]); return 2; }
        char line[128];
        unsigned pc, bank;
        while (fgets(line, sizeof line, m))
            if (sscanf(line, "MISS $%x bank %u", &pc, &bank) == 2 && pc < 0x8000 && bank < (unsigned)nbanks)
                region(regions, &cart, (int)bank, (int)(pc >> 14))->entries[pc] = 1, misses++;
        fclose(m);
    }
    for (int r = 0; r < nbanks * 2; r++)
        if (regions[r]) explore(regions[r]);
    const char *name = rom_path;
    for (const char *p = rom_path; *p; p++)
        if (*p == '/' || *p == '\\') name = p + 1;
    Buf o = {0};
    emit_program(&o, regions, nbanks, name, cart.rom, cart.rom_size, embed);
    buf_write_file(out, &o);
    long ninstr = 0, nregions = 0;
    for (int r = 0; r < nbanks * 2; r++) {
        if (!regions[r]) continue;
        nregions++;
        for (uint32_t a = 0; a < 0x10000; a++) ninstr += regions[r]->instr[a];
    }
    printf("gbrecomp: %s: %ld instructions in %ld bank regions (%ld trace entries)\n", name, ninstr, nregions, misses);
    return 0;
}

int main(int argc, char **argv) {
    if (argc == 3 && !strcmp(argv[1], "--cpu-switch")) {
        Buf o = {0};
        sm83_cpu_switch(&o);
        buf_write_file(argv[2], &o);
        return 0;
    }
    if (argc == 2 && !strcmp(argv[1], "--ops")) {
        Lines l;
        char name[32];
        for (int cb = 0; cb < 2; cb++)
            for (int i = 0; i < 256; i++) {
                sm83_body(i, cb, &l, name, sizeof name);
                printf("%s%02X %d %s\n", cb ? "CB" : "", i, cb ? 2 : sm83_size(i), name);
            }
        return 0;
    }
    if (argc >= 2 && argv[1][0] != '-') return recompile(argc, argv);
    fprintf(stderr, "usage: gbrecomp ROM -o OUT.c [--misses TRACE]... [--no-rom] | --cpu-switch OUT.inc | --ops\n");
    return 2;
}
