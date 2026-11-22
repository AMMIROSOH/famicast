/* Runs a .sms without a window, for tests and scripts: --frames N, --play
 * (button 1 taps to get past title screens, then walking right), --ppm FILE
 * (the last frame), --wav FILE, --state-at N FILE, --load-state FILE,
 * --pal/--ntsc. Prints the frame count, the PC, the VDP writes and how many
 * colours the last frame shows (a game stuck on a blank screen shows one). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "input_log.h"
#include "loader.h"
#include "sms.h"

static uint16_t scripted(long frame) {
    long k = frame % 60;
    uint16_t pad = frame >= 60 && k < 4 ? PAD_B : 0;
    if (frame > 600 && k >= 20) pad |= PAD_RIGHT;
    if (frame > 600 && k >= 40 && k < 44) pad |= PAD_A;
    return pad;
}

static int write_wav(FILE *w, long n) {  /* mono, 16-bit: the sizes once known */
    uint32_t data = (uint32_t)n * 2, riff = data + 36, rate = SMS_SAMPLE_RATE, bps = rate * 2;
    uint8_t h[44] = {'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'A', 'V', 'E', 'f', 'm', 't', ' ', 16, 0, 0, 0, 1, 0, 1, 0,
                     0, 0, 0, 0, 0, 0, 0, 0, 2, 0, 16, 0, 'd', 'a', 't', 'a'};
    memcpy(h + 4, &riff, 4), memcpy(h + 24, &rate, 4), memcpy(h + 28, &bps, 4), memcpy(h + 40, &data, 4);
    return fseek(w, 0, SEEK_SET) || fwrite(h, 1, 44, w) != 44;
}

int main(int argc, char **argv) {
    if (argc < 2 || argv[1][0] == '-') {
        fprintf(stderr, "usage: sms_headless ROM [--frames N] [--play] [--ppm FILE] [--wav FILE]\n"
                        "       [--state-at N FILE] [--load-state FILE] [--pal|--ntsc]\n");
        return 2;
    }
    const char *path = argv[1], *ppm = NULL, *wav = NULL, *state_out = NULL, *state_in = NULL;
    long frames = 600, state_at = -1;
    int play = 0, pal = strstr(path, "(Europe)") != NULL;
    for (int i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "--frames") && i + 1 < argc) frames = atol(argv[++i]);
        else if (!strcmp(argv[i], "--play")) play = 1;
        else if (!strcmp(argv[i], "--pal")) pal = 1;
        else if (!strcmp(argv[i], "--ntsc")) pal = 0;
        else if (!strcmp(argv[i], "--ppm") && i + 1 < argc) ppm = argv[++i];
        else if (!strcmp(argv[i], "--wav") && i + 1 < argc) wav = argv[++i];
        else if (!strcmp(argv[i], "--load-state") && i + 1 < argc) state_in = argv[++i];
        else if (!strcmp(argv[i], "--state-at") && i + 2 < argc) state_at = atol(argv[++i]), state_out = argv[++i];
        else { fprintf(stderr, "sms_headless: unknown option %s\n", argv[i]); return 2; }
    }
    uint8_t *rom;
    size_t size;
    char err[256];
    if (sms_load_rom(path, &rom, &size, err, sizeof err)) { fprintf(stderr, "%s\n", err); return 2; }
    static Sms s;
    sms_init(&s, rom, size, pal);
    if (state_in && sms_load(&s, state_in, err, sizeof err)) { fprintf(stderr, "%s\n", err); return 2; }
    FILE *w = wav ? fopen(wav, "wb") : NULL;
    if (wav && (!w || fseek(w, 44, SEEK_SET))) { perror(wav); return 2; }
    long samples = 0;
    for (long i = 0; i < frames; i++) {
        if (i == state_at && sms_save(&s, state_out)) { perror(state_out); return 2; }
        sms_frame(&s, play ? scripted(i) : 0, 0);
        for (unsigned k = 0; w && k < s.n_samples; k++, samples++) {
            float x = s.samples[k] < -1 ? -1 : s.samples[k] > 1 ? 1 : s.samples[k];
            int16_t v = (int16_t)(x * 32767);
            fwrite(&v, 2, 1, w);
        }
    }
    if (w && (write_wav(w, samples) || fclose(w))) { perror(wav); return 2; }
    uint8_t seen[64] = {0};  /* distinct colours (6-bit CRAM values), not CRAM entries */
    unsigned colours = 0;
    for (unsigned i = 0; i < SMS_WIDTH * SMS_HEIGHT; i++) {
        unsigned c = s.vdp.cram[s.vdp.frame[i] & 31];
        colours += !seen[c], seen[c] = 1;
    }
    printf("frames=%u pc=%04X vdp_writes=%u colours=%u\n", s.frames, s.cpu.pc, s.writes, colours);
    if (ppm) {
        FILE *o = fopen(ppm, "wb");
        if (!o) { perror(ppm); return 2; }
        fprintf(o, "P6\n%d %d\n255\n", SMS_WIDTH, SMS_HEIGHT);
        for (unsigned i = 0; i < SMS_WIDTH * SMS_HEIGHT; i++) {
            uint32_t c = sms_color(&s, s.vdp.frame[i]);
            fputc((int)(c >> 16 & 255), o), fputc((int)(c >> 8 & 255), o), fputc((int)(c & 255), o);
        }
        if (fclose(o)) { perror(ppm); return 2; }
    }
    free(rom);
    return 0;
}
