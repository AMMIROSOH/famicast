/* Game Boy without a window, for tests (tests/test_gb_roms.py).
 *   gb_headless ROM [--frames N]
 * Runs up to N frames (default 3600), stopping early when a test ROM
 * reports on the serial port: blargg's "Passed" / "Failed" text, or
 * mooneye's bytes 3 5 8 13 21 34 (pass) / six $42 (fail), or blargg's
 * result in cartridge RAM ($A000-). Prints what the serial port received (other bytes as \xHH) and a last line
 * "result: passed|failed|timeout". Exit code 0 only for passed. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gb.h"

static int result(const Gb *g) {  /* 1 pass, -1 fail, 0 not yet */
    static const char FIB[6] = {3, 5, 8, 13, 21, 34}, FAIL[6] = {0x42, 0x42, 0x42, 0x42, 0x42, 0x42};
    const char *s = g->serial_out;
    int n = g->serial_n;
    if (n >= 6 && !memcmp(s + n - 6, FIB, 6)) return 1;
    if (n >= 6 && !memcmp(s + n - 6, FAIL, 6)) return -1;
    char text[sizeof g->serial_out + 1];
    memcpy(text, s, (size_t)n);
    text[n] = 0;
    if (strstr(text, "Passed")) return 1;
    if (strstr(text, "Failed")) return -1;
    /* blargg's memory protocol: $A001 = DE B0 61, $A000 = $80 while running, then the result code */
    const uint8_t *m = g->cart.ram;
    if (g->cart.ram_size >= 0x2000 && m[1] == 0xDE && m[2] == 0xB0 && m[3] == 0x61 && m[0] != 0x80)
        return m[0] == 0 ? 1 : -1;
    return 0;
}

/* blargg's text at $A004, when it reports through memory */
static const char *memory_text(const Gb *g) {
    const uint8_t *m = g->cart.ram;
    if (g->cart.ram_size < 0x2000 || m[1] != 0xDE || m[2] != 0xB0 || m[3] != 0x61) return "";
    return (const char *)m + 4;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: gb_headless ROM [--frames N]\n");
        return 2;
    }
    long frames = 3600;
    for (int i = 2; i + 1 < argc; i++)
        if (!strcmp(argv[i], "--frames")) frames = atol(argv[++i]);
    FILE *f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 2; }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *rom = malloc((size_t)size);
    if (!rom || fread(rom, 1, (size_t)size, f) != (size_t)size) { perror(argv[1]); return 2; }
    fclose(f);
    static Gb g;
    char err[128];
    if (gb_load(&g, rom, (size_t)size, err, sizeof err) != 0) {
        fprintf(stderr, "%s: %s\n", argv[1], err);
        return 2;
    }
    free(rom);
    int r = 0;
    for (long i = 0; i < frames && !(r = result(&g)) && !g.cpu.jammed; i++) gb_run_frame(&g);
    for (int i = 0; i < g.serial_n; i++) {
        unsigned char ch = (unsigned char)g.serial_out[i];
        if (ch == '\n' || (ch >= 0x20 && ch < 0x7F)) putchar(ch);
        else printf("\\x%02X", ch);
    }
    if (!g.serial_n) printf("%.1024s", memory_text(&g));
    printf("\nresult: %s%s\n", r > 0 ? "passed" : r < 0 ? "failed" : "timeout", g.cpu.jammed ? " (CPU jammed)" : "");
    gb_free(&g);
    return r > 0 ? 0 : 1;
}
