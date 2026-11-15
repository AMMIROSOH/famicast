/* Headless NES runner for tests (driven by tests/test_roms.py and others).
 *
 *   nes_headless [ROM] [--frames N] [--blargg] [--peek ADDR] [--ppm FILE] [--hash]
 *                      [--press F:MASK]... [--record FILE] [--replay FILE] [--play SEED]
 *                      [--interp] [--diff] [--misses] [--coverage] [--out FILE]
 *                      [--load-state FILE] [--save-state FILE]
 *
 * ROM may be left out in recompiled builds: the ROM they embed is used.
 *
 * --blargg  run until the test ROM reports through $6000 (protocol: $6001-$6003
 *           = DE B0 61, $6000 = $80 running, $81 reset wanted, <$80 result,
 *           text at $6004; see nesdev "Emulator tests"). Exit code = result.
 * --peek    after N frames print the byte at ADDR (hex), e.g. $F8 for the
 *           2005 blargg tests, which store their result code there.
 * --ppm     write the last frame as a PPM image.
 * --hash    print an FNV-1a hash of the last frame and the CPU cycle count
 *           (regression tests).
 * --press F:MASK  hold controller-1 buttons MASK (hex, see nes.h) during
 *           frames F..F+5; may be repeated (scripted input for tests).
 * --record  save the input of every frame (runtime/input_log.h).
 * --replay  use a recording's input; N defaults to its length.
 * --play    scripted pseudo-random input from SEED (play_script below); N
 *           defaults to 6000. Drives coverage, lockstep tests and benchmarks.
 * --interp  ignore linked recompiled code (pure interpreter).
 * --diff    run a pure-interpreter NES in lockstep and compare CPU, RAM and PPU
 *           state after every recompiled block; stop at the first difference.
 * --misses  print how many instructions were interpreted, and where (input
 *           for nesrecomp --misses; without recompiled code, a full trace).
 * --coverage  print the share of CPU cycles run by recompiled code.
 * --text    after the last frame, print the screen's text (first nametable).
 * --load-state  start from a save state (runtime/nes.h; also for --diff).
 * --save-state  write a save state after the last frame.
 * --out     write the report to FILE instead of standard output (build steps).
 * --time    print the emulation time and speed relative to a real NES.
 * --wav     write the audio output (48 kHz mono, 16-bit) to a WAV file.
 * --audio-hash  print an FNV-1a hash of the 16-bit audio (regression tests).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

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
    nes_sync(a);
    nes_sync(b);
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
        n->cpu.yield = 1;  /* one block per step: compare after each */
        Cpu before = n->cpu;
        nes_step(n);
        while (ref->cpu.cycles < n->cpu.cycles && !ref->cpu.jammed) cpu_step(&ref->cpu);
        const char *what = compare(n, ref);
        if (!what) continue;
        printf("DIVERGENCE (%s) in frame %ld, step starting at PC=%04X%s\n", what, frame, before.pc,
               n->recomp && n->recomp(before.pc, nes_prg_bank(n, before.pc)) ? " (recompiled block)" : " (interpreted)");
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

/* Share of CPU cycles spent in recompiled code, and where the rest went.
 * Interrupt entries (7 cycles each) always run in the interpreter. */
static void print_coverage(Nes *n) {
    const uint64_t *f = n->cycles_fallback;
    double total = (double)(n->cycles_native + f[0] + f[1] + f[2]);
    if (total == 0) total = 1;
    printf("COVERAGE %.3f%% of CPU cycles recompiled; interpreted: ROM %.3f%%, RAM %.3f%%, "
           "interrupt entry %.3f%%\n", 100 * n->cycles_native / total, 100 * f[FALLBACK_ROM] / total,
           100 * f[FALLBACK_RAM] / total, 100 * f[FALLBACK_INTERRUPT] / total);
}

static void print_misses(Nes *n) {
    printf("STEPS native=%llu interpreted=%llu interrupt-entries=%llu\n",
           (unsigned long long)n->steps_native, (unsigned long long)n->steps_interp,
           (unsigned long long)n->steps_interrupt);
    print_coverage(n);
    nes_misses_write(n, stdout);
}

/* ---- audio capture (--wav, --audio-hash) ---- */
static int16_t *pcm;
static size_t pcm_len, pcm_cap;

static void collect_audio(Nes *n) {
    float tmp[APU_BUF];
    int k = apu_take_samples(&n->apu, tmp, APU_BUF);
    if (pcm_len + (size_t)k > pcm_cap) {
        pcm_cap = (pcm_len + (size_t)k) * 2;
        pcm = realloc(pcm, pcm_cap * sizeof *pcm);
    }
    for (int i = 0; i < k; i++) {
        float v = tmp[i] * 32767.0f;
        pcm[pcm_len++] = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
    }
}

static void put32(FILE *f, uint32_t v) { fwrite((uint8_t[4]){v, v >> 8, v >> 16, v >> 24}, 1, 4, f); }
static void put16(FILE *f, uint16_t v) { fwrite((uint8_t[2]){v, v >> 8}, 1, 2, f); }

static void write_wav(const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); exit(2); }
    uint32_t bytes = (uint32_t)(pcm_len * 2);
    fwrite("RIFF", 1, 4, f); put32(f, 36 + bytes); fwrite("WAVEfmt ", 1, 8, f);
    put32(f, 16); put16(f, 1); put16(f, 1); put32(f, APU_SAMPLE_RATE); put32(f, APU_SAMPLE_RATE * 2);
    put16(f, 2); put16(f, 16); fwrite("data", 1, 4, f); put32(f, bytes);
    for (size_t i = 0; i < pcm_len; i++) put16(f, (uint16_t)pcm[i]);
    fclose(f);
}

/* --play: a deterministic session. Start at frames 300 and 700 (as for
 * Battle City's title), then random D-pad directions held 20-90 frames with
 * A tapped every few frames, and Start twice every ~20 s (pause/unpause, or
 * a new game after a game over). Not a good player, but it goes through
 * menus and early levels the same way on every run. */
static uint32_t rng;

static uint32_t rnd(uint32_t n) {  /* xorshift32 */
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng % n;
}

static void play_script(InputLog *log, long n, unsigned seed) {
    static const uint8_t DIRS[5] = {PAD_UP, PAD_DOWN, PAD_LEFT, PAD_RIGHT, 0};
    static const int TAPS[3] = {6, 8, 12};
    log->frames = calloc((size_t)n + 1, sizeof *log->frames);
    log->count = n;
    rng = seed * 2654435761u + 1;
    for (long f = 300; f <= 700; f += 400)
        for (long i = f; i < f + 6 && i < n; i++) log->frames[i].pad[0] |= PAD_START;
    for (long f = 760; f < n;) {
        uint8_t dir = DIRS[rnd(5)];
        long end = f + 20 + (long)rnd(71);
        for (; f < end && f < n; f++) {
            log->frames[f].pad[0] |= dir;
            if (f % TAPS[rnd(3)] < 2) log->frames[f].pad[0] |= PAD_A;
        }
    }
    for (long f = 1900; f < n; f += 1200)
        for (long s = f; s <= f + 90; s += 90)
            for (long i = s; i < s + 6 && i < n; i++) log->frames[i].pad[0] = PAD_START;
}

static int blargg_ready(Nes *n) {
    return nes_peek(n, 0x6001) == 0xDE && nes_peek(n, 0x6002) == 0xB0 && nes_peek(n, 0x6003) == 0x61;
}

/* --text: the first nametable as text, for test ROMs that only print to
 * the screen (blargg's font has tiles at their ASCII codes). */
static void print_screen_text(Nes *n) {
    for (int row = 0; row < 30; row++) {
        char line[33];
        int end = 0;
        for (int col = 0; col < 32; col++) {
            uint8_t t = n->ppu.nt[cart_nt_index(&n->cart, (uint16_t)(0x2000 + row * 32 + col))];
            line[col] = t >= 0x20 && t < 0x7F ? (char)t : ' ';
            if (line[col] != ' ') end = col + 1;
        }
        line[end] = 0;
        if (end) printf("%s\n", line);
    }
}

static void print_blargg_text(Nes *n) {
    for (uint16_t a = 0x6004; a < 0x8000; a++) {
        uint8_t ch = nes_peek(n, a);
        if (!ch) break;
        putchar(ch);
    }
}

int main(int argc, char **argv) {
    const char *rom_path = argc > 1 && argv[1][0] != '-' ? argv[1] : NULL;
    if (!rom_path && !(recomp_program && recomp_program->rom)) {
        fprintf(stderr, "usage: nes_headless ROM [--frames N] [--blargg] [--peek ADDR] [--ppm FILE] [--hash] ...\n");
        return 2;
    }
    long frames = -1, peek = -1, play_seed = -1;
    int blargg = 0, hash = 0, npress = 0, interp = 0, diff = 0, misses = 0, timing = 0, audio_hash = 0, coverage = 0, text = 0;
    const char *wav = NULL;
    long press_frame[64];
    unsigned press_mask[64];
    const char *ppm = NULL, *record = NULL, *replay = NULL, *load_state = NULL, *save_state = NULL;
    for (int i = rom_path ? 2 : 1; i < argc; i++) {
        if (!strcmp(argv[i], "--frames") && i + 1 < argc) frames = atol(argv[++i]);
        else if (!strcmp(argv[i], "--peek") && i + 1 < argc) peek = strtol(argv[++i], NULL, 16);
        else if (!strcmp(argv[i], "--ppm") && i + 1 < argc) ppm = argv[++i];
        else if (!strcmp(argv[i], "--blargg")) blargg = 1;
        else if (!strcmp(argv[i], "--hash")) hash = 1;
        else if (!strcmp(argv[i], "--interp")) interp = 1;
        else if (!strcmp(argv[i], "--diff")) diff = 1;
        else if (!strcmp(argv[i], "--misses")) misses = 1;
        else if (!strcmp(argv[i], "--coverage")) coverage = 1;
        else if (!strcmp(argv[i], "--text")) text = 1;
        else if (!strcmp(argv[i], "--load-state") && i + 1 < argc) load_state = argv[++i];
        else if (!strcmp(argv[i], "--save-state") && i + 1 < argc) save_state = argv[++i];
        else if (!strcmp(argv[i], "--time")) timing = 1;
        else if (!strcmp(argv[i], "--wav") && i + 1 < argc) wav = argv[++i];
        else if (!strcmp(argv[i], "--audio-hash")) audio_hash = 1;
        else if (!strcmp(argv[i], "--record") && i + 1 < argc) record = argv[++i];
        else if (!strcmp(argv[i], "--replay") && i + 1 < argc) replay = argv[++i];
        else if (!strcmp(argv[i], "--play") && i + 1 < argc) play_seed = atol(argv[++i]);
        else if (!strcmp(argv[i], "--out") && i + 1 < argc) {
            if (!freopen(argv[++i], "w", stdout)) { perror(argv[i]); return 2; }
        }
        else if (!strcmp(argv[i], "--press") && i + 1 < argc && npress < 64 &&
                 sscanf(argv[++i], "%ld:%x", &press_frame[npress], &press_mask[npress]) == 2) npress++;
        else { fprintf(stderr, "unknown option %s\n", argv[i]); return 2; }
    }
    long size;
    const uint8_t *rom;
    uint8_t *rom_file = NULL;
    if (rom_path) rom = rom_file = read_file(rom_path, &size);
    else rom = recomp_program->rom, size = (long)recomp_program->rom_size, rom_path = recomp_program->rom_name;
    static Nes nes;
    char err[128];
    if (nes_load(&nes, rom, size, err, sizeof err) != 0) {
        fprintf(stderr, "%s: %s\n", rom_path, err);
        return 2;
    }
    if (interp) nes.recomp = NULL;
    else if (recomp_program && !nes.recomp)
        printf("WARNING: recompiled code is for %s; this ROM's PRG differs, interpreting\n",
               recomp_program->rom_name);
    if (misses) nes.misses = calloc(nes_miss_slots(&nes), sizeof *nes.misses);
    static Nes ref;
    if (diff) {
        if (!nes.recomp) { fprintf(stderr, "--diff needs matching recompiled code\n"); return 2; }
        nes_load(&ref, rom, size, err, sizeof err);
        ref.recomp = NULL;
    }
    if (load_state && (nes_state_load(&nes, load_state, err, sizeof err) != 0 ||
                       (diff && nes_state_load(&ref, load_state, err, sizeof err) != 0))) {
        fprintf(stderr, "%s: %s\n", load_state, err);
        return 2;
    }
    InputLog log = {0};
    if (replay && input_log_load(replay, &log) != 0) {
        fprintf(stderr, "%s: not an input recording\n", replay);
        return 2;
    }
    FILE *rec = NULL;
    if (record && !(rec = input_log_create(record))) { perror(record); return 2; }
    if (play_seed >= 0) {
        if (frames < 0) frames = 6000;
        play_script(&log, frames, (unsigned)play_seed);
    }
    if (frames < 0) frames = replay ? log.count : blargg ? 3600 : 60;

    int result = 0;
    long reset_at = -1, frames_run = 0;
    struct timespec t0, t1;
    timespec_get(&t0, TIME_UTC);
    for (long f = 0; f < frames; f++) {
        InputFrame in = {{0, 0}, 0};
        if (f < log.count) in = log.frames[f];
        for (int i = 0; i < npress; i++)
            if (f >= press_frame[i] && f < press_frame[i] + 6) in.pad[0] |= (uint8_t)press_mask[i];
        if (rec) input_log_write(rec, in);
        if (in.flags & INPUT_RESET) { nes_reset(&nes); if (diff) nes_reset(&ref); }
        frames_run++;
        if (wav || audio_hash) collect_audio(&nes);
        else nes.apu.buf_len = 0;
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
        printf("HASH %08X cycles=%llu\n", h, (unsigned long long)nes.cpu.cycles);
    }
    timespec_get(&t1, TIME_UTC);
    if (timing) {
        double sec = (double)(t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9;
        printf("TIME %.2f s for %ld frames (%.1fx real time)\n", sec, frames_run, frames_run / 60.0988 / sec);
    }
    if (diff) printf("DIFF OK: %ld frames, recompiled and interpreter identical\n", frames);
    if (text) print_screen_text(&nes);
    if (misses) print_misses(&nes);
    else if (coverage) print_coverage(&nes);
    if (save_state && nes_state_save(&nes, save_state) != 0) { perror(save_state); return 2; }
    if (ppm) write_ppm(&nes, ppm);
    if (wav) write_wav(wav);
    if (audio_hash) {
        uint32_t h = 2166136261u;
        for (size_t i = 0; i < pcm_len; i++) {
            h = (h ^ (uint8_t)pcm[i]) * 16777619u;
            h = (h ^ (uint8_t)(pcm[i] >> 8)) * 16777619u;
        }
        printf("AUDIOHASH %08X (%zu samples)\n", h, pcm_len);
    }
    if (rec) fclose(rec);
    input_log_free(&log);
    cart_free(&nes.cart);
    free(rom_file);
    return result;
}
