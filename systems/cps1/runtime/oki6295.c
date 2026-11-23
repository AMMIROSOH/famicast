/* The OKI MSM6295 (oki6295.h). A byte with bit 7 set picks a phrase; the
 * next starts it on the voice of its bits 4-7 at the attenuation of bits
 * 0-3. A byte with bit 7 clear stops the voices of its bits 3-6. A phrase's
 * table entry (8 bytes at phrase x 8) holds its first and last byte, 18
 * bits each, big-endian; a voice still playing ignores a start, and so
 * does a phrase whose last byte is not after its first. ADPCM is OKI's
 * (the Dialogic kind): 4 bits a sample, high nibble first, 49 steps, a
 * 12-bit signal. Bit-exact with MAME 0.289's okim6295 on Final Fight.
 * Sources: docs/sources.md (CPS-1 sound). */
#include <string.h>

#include "oki6295.h"

static const int16_t STEP[49] = {16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97,
                                 107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449,
                                 494, 544, 598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552};
static const int8_t ADJUST[8] = {-1, -1, -1, -1, 2, 4, 6, 8};
static const uint8_t VOLUME[16] = {32, 22, 16, 11, 8, 6, 4, 3, 2};  /* 0 to -24 dB, ~3 dB a step; 9-15 silent */

void oki_reset(Oki *o, const uint8_t *rom, uint32_t size) {
    memset(o, 0, sizeof *o);
    o->rom = rom, o->size = size, o->pending = -1, o->pin7 = 1;
}

static uint32_t addr18(const uint8_t *p) { return (uint32_t)(p[0] << 16 | p[1] << 8 | p[2]) & 0x3FFFF; }

void oki_write(Oki *o, uint8_t v) {
    if (o->pending >= 0) {
        uint32_t at = (uint32_t)o->pending * 8;
        o->pending = -1;
        if (at + 6 > o->size) return;
        uint32_t start = addr18(o->rom + at), end = addr18(o->rom + at + 3);
        for (int i = 0; i < 4; i++)
            if (v >> (4 + i) & 1) {
                OkiVoice *s = &o->v[i];
                /* a voice still playing ignores it; so does a phrase not ending after its start */
                if (s->playing || start >= end || start >= o->size) continue;
                memset(s, 0, sizeof *s);
                s->playing = 1, s->start = start, s->count = 2 * (end - start + 1), s->vol = VOLUME[v & 15];
            }
    } else if (v & 0x80) {
        o->pending = v & 0x7F;
    } else {
        for (int i = 0; i < 4; i++)
            if (v >> (3 + i) & 1) o->v[i].playing = 0;
    }
}

uint8_t oki_status(const Oki *o) {
    uint8_t s = 0xF0;
    for (int i = 0; i < 4; i++) s |= (uint8_t)(o->v[i].playing << i);
    return s;
}

static int voice(Oki *o, OkiVoice *s) {
    if (!s->playing) return 0;
    uint32_t at = s->start + s->nibble / 2;
    int byte = at < o->size ? o->rom[at] : 0;  /* past the ROM: silence */
    int n = s->nibble & 1 ? byte & 15 : byte >> 4;
    int step = STEP[s->step], diff = step >> 3;
    if (n & 1) diff += step >> 2;
    if (n & 2) diff += step >> 1;
    if (n & 4) diff += step;
    int sig = s->signal + (n & 8 ? -diff : diff);
    s->signal = (int16_t)(sig > 2047 ? 2047 : sig < -2048 ? -2048 : sig);
    int st = s->step + ADJUST[n & 7];
    s->step = (int8_t)(st < 0 ? 0 : st > 48 ? 48 : st);
    if (++s->nibble >= s->count) s->playing = 0;  /* the last nibble ends it */
    return s->signal * s->vol;
}

int oki_sample(Oki *o) {
    int sum = 0;
    for (int i = 0; i < 4; i++) sum += voice(o, &o->v[i]);
    return sum;
}
