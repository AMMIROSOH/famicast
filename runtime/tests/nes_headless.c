/* Headless NES runner for tests (driven by tests/test_ppu.py).
 *
 *   nes_headless ROM [--frames N] [--blargg] [--peek ADDR] [--ppm FILE] [--hash]
 *                    [--press F:MASK]... [--record FILE] [--replay FILE]
 *                    [--interp] [--diff] [--misses]
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
 * --record  save the input of every frame (runtime/input_log.h).
 * --replay  use a recording's input; N defaults to its length.
 * --interp  ignore linked recompiled code (pure interpreter).
 * --diff    run a pure-interpreter NES in lockstep and compare CPU, RAM and PPU
 *           state after every recompiled block; stop at the first difference.
 * --misses  print how many instructions were interpreted, and where.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "input_log.h"
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

/* ---- lockstep comparison (--diff) ---- */
static const char *compare(Nes *a, Nes *b) {
    const Cpu *x = &a->cpu, *y = &b->cpu;
    if (x->pc != y->pc || x->a != y->a || x->x != y->x || x->y != y->y || x->s != y->s ||
        x->p != y->p || x->cycles != y->cycles || x->nmi != y->nmi || x->poll_prev != y->poll_prev)
        return "CPU state";
    if (memcmp(a->ram, b->ram, sizeof a->ram)) return "RAM";
    if (memcmp(a->cart.prg_ram, b->cart.prg_ram, sizeof a->cart.prg_ram)) return "PRG RAM";
    const Ppu *p = &a->ppu, *q = &b->ppu;
    if (p->scanline != q->scanline || p->dot != q->dot || p->v != q->v || p->t != q->t ||
        p->status != q->status || p->ctrl != q->ctrl || p->mask != q->mask ||
        memcmp(p->oam, q->oam, sizeof p->oam) || memcmp(p->nt, q->nt, sizeof p->nt) ||
        memcmp(p->pal, q->pal, sizeof p->pal))
        return "PPU state";
    return NULL;
}

static void print_cpu(const char *who, const Cpu *c) {
    printf("  %-11s PC=%04X A=%02X X=%02X Y=%02X P=%02X S=%02X cycles=%llu nmi=%d poll=%d\n", who,
           c->pc, c->a, c->x, c->y, c->p, c->s, (unsigned long long)c->cycles, c->nmi, c->poll_prev);
}

/* Returns 0, or -1 after printing the first divergence. */
static int run_frame_diff(Nes *n, Nes *ref, long frame) {
    n->ppu.frame_done = 0;
    while (!n->ppu.frame_done && !n->cpu.jammed) {
        Cpu before = n->cpu;
        nes_step(n);
        while (ref->cpu.cycles < n->cpu.cycles && !ref->cpu.jammed) cpu_step(&ref->cpu);
        const char *what = compare(n, ref);
        if (!what) continue;
        printf("DIVERGENCE (%s) in frame %ld, step starting at PC=%04X%s\n", what, frame, before.pc,
               n->recomp && n->recomp(before.pc) ? " (recompiled block)" : " (interpreted)");
        print_cpu("before", &before);
        print_cpu("recompiled", &n->cpu);
        print_cpu("interpreter", &ref->cpu);
        for (int i = 0, shown = 0; i < 0x800 && shown < 8; i++)
            if (n->ram[i] != ref->ram[i]) {
                printf("  RAM[%03X] recompiled=%02X interpreter=%02X\n", i, n->ram[i], ref->ram[i]);
                shown++;
            }
        return -1;
    }
    return 0;
}

static void print_misses(Nes *n) {
    printf("STEPS native=%llu interpreted=%llu interrupt-entries=%llu\n",
           (unsigned long long)n->steps_native, (unsigned long long)n->steps_interp,
           (unsigned long long)n->steps_interrupt);
    for (uint32_t a = 0; a < 0x10000; a++)  /* input for python -m recomp.suggest */
        if (n->misses[a]) printf("MISS $%04X %u%s\n", a, n->misses[a], a < 0x8000 ? " (RAM)" : "");
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
    int blargg = 0, hash = 0, npress = 0, interp = 0, diff = 0, misses = 0;
    long press_frame[64];
    unsigned press_mask[64];
    const char *ppm = NULL, *record = NULL, *replay = NULL;
    for (int i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "--frames") && i + 1 < argc) frames = atol(argv[++i]);
        else if (!strcmp(argv[i], "--peek") && i + 1 < argc) peek = strtol(argv[++i], NULL, 16);
        else if (!strcmp(argv[i], "--ppm") && i + 1 < argc) ppm = argv[++i];
        else if (!strcmp(argv[i], "--blargg")) blargg = 1;
        else if (!strcmp(argv[i], "--hash")) hash = 1;
        else if (!strcmp(argv[i], "--interp")) interp = 1;
        else if (!strcmp(argv[i], "--diff")) diff = 1;
        else if (!strcmp(argv[i], "--misses")) misses = 1;
        else if (!strcmp(argv[i], "--record") && i + 1 < argc) record = argv[++i];
        else if (!strcmp(argv[i], "--replay") && i + 1 < argc) replay = argv[++i];
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
    if (interp) nes.recomp = NULL;
    else if (recomp_program && !nes.recomp)
        printf("WARNING: recompiled code is for %s; this ROM's PRG differs, interpreting\n",
               recomp_program->rom_name);
    if (misses) nes.misses = calloc(0x10000, sizeof *nes.misses);
    static Nes ref;
    if (diff) {
        if (!nes.recomp) { fprintf(stderr, "--diff needs matching recompiled code\n"); return 2; }
        nes_load(&ref, rom, size, err, sizeof err);
        ref.recomp = NULL;
    }
    InputLog log = {0};
    if (replay && input_log_load(replay, &log) != 0) {
        fprintf(stderr, "%s: not an input recording\n", replay);
        return 2;
    }
    FILE *rec = NULL;
    if (record && !(rec = input_log_create(record))) { perror(record); return 2; }
    if (frames < 0) frames = replay ? log.count : blargg ? 3600 : 60;

    int result = 0;
    long reset_at = -1;
    for (long f = 0; f < frames; f++) {
        InputFrame in = {{0, 0}, 0};
        if (f < log.count) in = log.frames[f];
        for (int i = 0; i < npress; i++)
            if (f >= press_frame[i] && f < press_frame[i] + 6) in.pad[0] |= (uint8_t)press_mask[i];
        if (rec) input_log_write(rec, in);
        if (in.flags & INPUT_RESET) { nes_reset(&nes); if (diff) nes_reset(&ref); }
        nes.pad[0] = ref.pad[0] = in.pad[0];
        nes.pad[1] = ref.pad[1] = in.pad[1];
        if (!diff) nes_run_frame(&nes);
        else if (run_frame_diff(&nes, &ref, f) != 0) return 253;
        else if (memcmp(nes.ppu.fb, ref.ppu.fb, sizeof nes.ppu.fb)) {
            printf("DIVERGENCE (frame image) in frame %ld\n", f);
            return 253;
        }
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
    if (diff) printf("DIFF OK: %ld frames, recompiled and interpreter identical\n", frames);
    if (misses) print_misses(&nes);
    if (ppm) write_ppm(&nes, ppm);
    if (rec) fclose(rec);
    input_log_free(&log);
    cart_free(&nes.cart);
    free(rom);
    return result;
}
