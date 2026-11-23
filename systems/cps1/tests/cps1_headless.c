/* Runs a CPS-1 game (a zip) without a window, for tests and scripts:
 * --frames N, --play (a coin and Start for player 1, then walking right and
 * tapping button 1), --ppm FILE (the last frame). Prints the game, the
 * frames run and the colours in the last frame. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cps1.h"
#include "input_log.h"

static uint16_t scripted(long frame) {
    long k = frame % 60;
    if (frame >= 300 && frame < 304) return PAD_SELECT;  /* a coin, once the game has booted */
    if (frame >= 400 && frame < 404) return PAD_START;
    uint16_t pad = 0;
    if (frame > 600 && k >= 20) pad |= PAD_RIGHT;
    if (frame > 600 && k < 4) pad |= PAD_B;
    return pad;
}

static uint8_t *read_file(const char *path, size_t *size) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *data = n > 0 ? malloc((size_t)n) : NULL;
    if (data && fread(data, 1, (size_t)n, f) != (size_t)n) free(data), data = NULL;
    fclose(f);
    *size = (size_t)n;
    return data;
}

static Cps1 machine;

int main(int argc, char **argv) {
    const char *path = NULL, *ppm = NULL;
    long frames = 60;
    int play = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--frames") && i + 1 < argc) frames = atol(argv[++i]);
        else if (!strcmp(argv[i], "--ppm") && i + 1 < argc) ppm = argv[++i];
        else if (!strcmp(argv[i], "--play")) play = 1;
        else if (argv[i][0] != '-') path = argv[i];
        else return fprintf(stderr, "unknown option %s\n", argv[i]), 2;
    }
    if (!path) return fprintf(stderr, "usage: cps1_headless GAME.zip [--frames N] [--play] [--ppm FILE]\n"), 2;
    size_t size;
    uint8_t *data = read_file(path, &size);
    if (!data) return fprintf(stderr, "%s: cannot read\n", path), 1;
    Zip z;
    const Cps1Game *g = zip_open(&z, data, size) ? NULL : cps1_match(&z);
    if (!g) return fprintf(stderr, "%s: not a CPS-1 game this program knows\n", path), 1;
    Cps1Rom rom;
    char err[256];
    if (cps1_load(g, &z, &rom, err, sizeof err)) return fprintf(stderr, "%s\n", err), 1;
    zip_close(&z);
    free(data);

    cps1_init(&machine, &rom);
    for (long f = 0; f < frames; f++) {
        cps1_set_input(&machine, 0, play ? scripted(f) : 0);
        cps1_frame(&machine);
    }

    static uint8_t seen[0xC00];
    int colours = 0;
    for (int i = 0; i < CPS1_WIDTH * CPS1_HEIGHT; i++) {
        uint16_t c = machine.frame[i] % 0xC00;
        if (!seen[c]) seen[c] = 1, colours++;
    }
    if (ppm) {
        FILE *f = fopen(ppm, "wb");
        if (!f) return perror(ppm), 1;
        fprintf(f, "P6\n%d %d\n255\n", CPS1_WIDTH, CPS1_HEIGHT);
        for (int i = 0; i < CPS1_WIDTH * CPS1_HEIGHT; i++) {
            uint32_t c = cps1_color(&machine, machine.frame[i]);
            uint8_t px[3] = {(uint8_t)(c >> 16), (uint8_t)(c >> 8), (uint8_t)c};
            fwrite(px, 1, 3, f);
        }
        fclose(f);
    }
    printf("game=%s frames=%ld colours=%d%s\n", g->id, frames, colours, machine.cpu.halted ? " halted" : "");
    cps1_unload(&rom);
    return machine.cpu.halted ? 1 : 0;
}
