#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "atari.h"
#include "loader.h"

int main(int argc, char **argv) {
    const char *path = argc > 1 && argv[1][0] != '-' ? argv[1] : NULL;
    int embedded = !path && a26_recomp_program && a26_recomp_program->rom;
    if (!path && !embedded) { fprintf(stderr, "usage: atari2600_headless ROM [--frames N] [--diff] [--coverage]\n"); return 2; }
    const char *name = path ? path : a26_recomp_program->rom_name;
    int frames = 60, play = 0, diff = 0, coverage = 0;
    int pal = a26_recomp_program ? a26_recomp_program->pal : (strstr(name, "(Europe)") != NULL || strstr(name, "(PAL)") != NULL);
    A26CartType mapper = A26_CART_AUTO;
    if (a26_recomp_program) mapper = (A26CartType)a26_recomp_program->mapper;
    const char *ppm = NULL, *trace_path = NULL, *load_path = NULL, *save_path = NULL;
    int save_at = -1;
    for (int i = path ? 2 : 1; i < argc; i++) {
        if (!strcmp(argv[i], "--frames") && i + 1 < argc) frames = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--play")) play = 1;
        else if (!strcmp(argv[i], "--diff")) diff = 1;
        else if (!strcmp(argv[i], "--coverage")) coverage = 1;
        else if (!strcmp(argv[i], "--trace") && i + 1 < argc) trace_path = argv[++i];
        else if (!strcmp(argv[i], "--load-state") && i + 1 < argc) load_path = argv[++i];
        else if (!strcmp(argv[i], "--state-at") && i + 2 < argc) { save_at = atoi(argv[++i]); save_path = argv[++i]; }
        else if (!strcmp(argv[i], "--pal")) pal = 1;
        else if (!strcmp(argv[i], "--region") && i + 1 < argc) {
            const char *value = argv[++i];
            if (!strcmp(value, "ntsc")) pal = 0;
            else if (!strcmp(value, "pal")) pal = 1;
            else if (!strcmp(value, "auto")) pal = a26_recomp_program ? a26_recomp_program->pal :
                (strstr(name, "(Europe)") != NULL || strstr(name, "(PAL)") != NULL);
            else { fprintf(stderr, "invalid region: %s\n", value); return 2; }
        } else if (!strcmp(argv[i], "--mapper") && i + 1 < argc) {
            static const char *const names[] = {"auto", "2k", "4k", "f8", "f6", "f4"};
            const char *value = argv[++i];
            unsigned j;
            for (j = 0; j < sizeof names / sizeof names[0]; j++) if (!strcmp(value, names[j])) break;
            if (j == sizeof names / sizeof names[0]) { fprintf(stderr, "invalid mapper: %s\n", value); return 2; }
            mapper = (A26CartType)j;
        }
        else if (!strcmp(argv[i], "--ppm") && i + 1 < argc) ppm = argv[++i];
        else { fprintf(stderr, "unknown option: %s\n", argv[i]); return 2; }
    }
    uint8_t *rom;
    size_t size;
    char err[256];
    if (embedded) {
        size = a26_recomp_program->rom_size;
        rom = malloc(size);
        if (!rom) return 1;
        memcpy(rom, a26_recomp_program->rom, size);
    } else if (a26_load_rom(path, &rom, &size, err, sizeof err)) { fprintf(stderr, "%s\n", err); return 1; }
    Atari2600 machine;
    if (a26_init(&machine, rom, size, mapper, pal)) { fprintf(stderr, "unsupported cartridge\n"); free(rom); return 1; }
    if (a26_recomp_program && !machine.program) { fprintf(stderr, "ROM or hardware configuration does not match compiled code\n"); free(rom); return 1; }
    Atari2600 reference;
    if (diff) {
        if (!machine.program) { fprintf(stderr, "--diff needs recompiled code\n"); free(rom); return 2; }
        if (a26_init(&reference, rom, size, mapper, pal)) { free(rom); return 1; }
        reference.program = NULL;
        machine.reference = &reference;
    }
    uint8_t trace[8 * 4096] = {0};
    if (trace_path) machine.trace = trace;
    if (load_path && (a26_load(&machine, load_path) || (diff && a26_load(&reference, load_path)))) {
        fprintf(stderr, "cannot load state\n"); free(rom); return 1;
    }
    InputFrame input = {0};
    for (int i = 0; i < frames && !machine.bus.cpu.jammed; i++) {
        if (play) input.pad[0] = (uint16_t)((i >= 30 && i < 35 ? PAD_START : 0) |
                                           (i >= 60 && i % 20 < 10 ? PAD_A : 0));
        a26_frame(&machine, &input);
        machine.tia.sample_count = 0;
        if (diff) reference.tia.sample_count = 0;
        if (save_path && i + 1 == save_at && a26_save(&machine, save_path)) {
            fprintf(stderr, "cannot save state\n"); free(rom); return 1;
        }
        if (machine.diff_failed) break;
    }
    printf("frames=%u cycles=%llu pc=%04X bank=%u jammed=%u\n", machine.tia.frames,
           (unsigned long long)machine.bus.cpu.cycles, machine.bus.cpu.pc,
           machine.bus.cart.bank, machine.bus.cpu.jammed);
    if (diff && !machine.diff_failed) printf("diff=matched\n");
    if (coverage) printf("coverage=%.2f%% compiled_cycles=%llu total_cycles=%llu\n",
        machine.total_cycles ? 100.0 * (double)machine.compiled_cycles / (double)machine.total_cycles : 0.0,
        (unsigned long long)machine.compiled_cycles, (unsigned long long)machine.total_cycles);
    if (trace_path) {
        FILE *f = fopen(trace_path, "wb");
        if (!f || fwrite(trace, 1, sizeof trace, f) != sizeof trace) {
            if (f) fclose(f);
            fprintf(stderr, "cannot write trace\n"); free(rom); return 1;
        }
        if (fclose(f)) { free(rom); return 1; }
    }
    if (ppm) {
        FILE *f = fopen(ppm, "wb");
        if (!f) { free(rom); return 1; }
        fprintf(f, "P5\n%d %d\n255\n", A26_WIDTH, A26_HEIGHT);
        for (unsigned i = 0; i < A26_WIDTH * A26_HEIGHT; i++) fputc((machine.tia.frame[i] & 14) * 17, f);
        fclose(f);
    }
    int result = machine.bus.cpu.jammed || machine.diff_failed ? 1 : 0;
    free(rom);
    return result;
}
