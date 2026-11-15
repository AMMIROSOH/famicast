/* Headless NES runner for tests (driven by tests/test_ppu.py).
 *
 *   nes_headless ROM [--frames N] [--blargg] [--peek ADDR] [--ppm FILE] [--hash]
 *
 * --blargg  run until the test ROM reports through $6000 (protocol: $6001-$6003
 *           = DE B0 61, $6000 = $80 running, $81 reset wanted, <$80 result,
 *           text at $6004; see nesdev "Emulator tests"). Exit code = result.
 * --peek    after N frames print the byte at ADDR (hex), e.g. $F8 for the
 *           2005 blargg tests, which store their result code there.
 * --ppm     write the last frame as a PPM image.
 * --hash    print an FNV-1a hash of the last frame (regression tests).
 * --press F:MASK  hold controller-1 buttons MASK (hex, see nes.h) during
 *           frames F..F+5; may be repeated (scripted input for tests).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nes.h"

static uint8_t *read_file(const char *path, long *size) {
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); exit(2); }
    fseek(f, 0, SEEK_END);
    *size = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *buf = malloc(*size);
    if (fread(buf, 1, *size, f) != (size_t)*size) { perror(path); exit(2); }
    fclose(f);
    return buf;
}

static void write_ppm(Nes *n, const char *path) {
    uint32_t pal[64];
    nes_palette_rgb(pal);
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); exit(2); }
    fprintf(f, "P6 256 240 255\n");
    for (int i = 0; i < 256 * 240; i++) {
        uint32_t c = pal[n->ppu.fb[i] & 0x3F];
        uint8_t rgb[3] = {(uint8_t)(c >> 16), (uint8_t)(c >> 8), (uint8_t)c};
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
}

static int blargg_ready(Nes *n) {
    return nes_peek(n, 0x6001) == 0xDE && nes_peek(n, 0x6002) == 0xB0 && nes_peek(n, 0x6003) == 0x61;
}

static void print_blargg_text(Nes *n) {
    for (uint16_t a = 0x6004; a < 0x8000; a++) {
        uint8_t ch = nes_peek(n, a);
        if (!ch) break;
        putchar(ch);
    }
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: nes_headless ROM [--frames N] [--blargg] [--peek ADDR] [--ppm FILE] [--hash]\n");
        return 2;
    }
    long frames = -1, peek = -1;
    int blargg = 0, hash = 0, npress = 0;
    long press_frame[64];
    unsigned press_mask[64];
    const char *ppm = NULL;
    for (int i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "--frames") && i + 1 < argc) frames = atol(argv[++i]);
        else if (!strcmp(argv[i], "--peek") && i + 1 < argc) peek = strtol(argv[++i], NULL, 16);
        else if (!strcmp(argv[i], "--ppm") && i + 1 < argc) ppm = argv[++i];
        else if (!strcmp(argv[i], "--blargg")) blargg = 1;
        else if (!strcmp(argv[i], "--hash")) hash = 1;
        else if (!strcmp(argv[i], "--press") && i + 1 < argc && npress < 64 &&
                 sscanf(argv[++i], "%ld:%x", &press_frame[npress], &press_mask[npress]) == 2) npress++;
        else { fprintf(stderr, "unknown option %s\n", argv[i]); return 2; }
    }
    long size;
    uint8_t *rom = read_file(argv[1], &size);
    static Nes nes;
    char err[128];
    if (nes_load(&nes, rom, size, err, sizeof err) != 0) {
        fprintf(stderr, "%s: %s\n", argv[1], err);
        return 2;
    }
    if (frames < 0) frames = blargg ? 3600 : 60;

    int result = 0;
    long reset_at = -1;
    for (long f = 0; f < frames; f++) {
        nes.pad[0] = 0;
        for (int i = 0; i < npress; i++)
            if (f >= press_frame[i] && f < press_frame[i] + 6) nes.pad[0] |= (uint8_t)press_mask[i];
        nes_run_frame(&nes);
        if (nes.cpu.jammed) {
            printf("CPU jammed at frame %ld, PC=$%04X\n", f, nes.cpu.pc);
            return 254;
        }
        if (!blargg || !blargg_ready(&nes)) continue;
        uint8_t st = nes_peek(&nes, 0x6000);
        if (st == 0x81 && reset_at < 0) reset_at = f + 10;  /* "press reset after >= 100 ms" */
        if (reset_at >= 0 && f == reset_at) { nes_reset(&nes); reset_at = -1; }
        if (st < 0x80) {
            print_blargg_text(&nes);
            printf("\nRESULT %d after %ld frames\n", st, f + 1);
            result = st;
            break;
        }
        if (f == frames - 1) {
            print_blargg_text(&nes);
            printf("\nTIMEOUT after %ld frames\n", frames);
            result = 255;
        }
    }
    if (blargg && !blargg_ready(&nes)) { printf("TIMEOUT: no $6000 signature\n"); result = 255; }
    if (peek >= 0) printf("PEEK $%04lX = %02X\n", peek, nes_peek(&nes, (uint16_t)peek));
    if (hash) {
        uint32_t h = 2166136261u;
        for (int i = 0; i < 256 * 240; i++) h = (h ^ nes.ppu.fb[i]) * 16777619u;
        printf("HASH %08X\n", h);
    }
    if (ppm) write_ppm(&nes, ppm);
    cart_free(&nes.cart);
    free(rom);
    return result;
}
