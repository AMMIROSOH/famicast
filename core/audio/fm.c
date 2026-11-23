/* The FM operator (fm.h), from the Genesis' YM2612 (systems/genesis/runtime/sound.c
 * keeps that chip's channels; sources there and in docs/sources.md). */
#include "fm.h"

#include <math.h>

static uint16_t LOGSIN[256], EXP[256];  /* quarter sine as -log2 * 256; 2^x fraction * 1024 */

void fm_tables(void) {
    if (EXP[255]) return;
    for (int i = 0; i < 256; i++) {  /* integers: the same on every platform */
        LOGSIN[i] = (uint16_t)lround(-log2(sin((i + 0.5) * 3.14159265358979 / 512)) * 256);
        EXP[i] = (uint16_t)lround((exp2(i / 256.0) - 1) * 1024);
    }
}

static const uint8_t DT[4][32] = {  /* detune by key code, in phase increments */
    {0},
    {0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 3, 3, 3, 4, 4, 4, 5, 5, 6, 6, 7, 8, 8, 8, 8},
    {1, 1, 1, 1, 2, 2, 2, 2, 2, 3, 3, 3, 4, 4, 4, 5, 5, 6, 6, 7, 8, 8, 9, 10, 11, 12, 13, 14, 16, 16, 16, 16},
    {2, 2, 2, 2, 2, 3, 3, 3, 4, 4, 4, 5, 5, 6, 6, 7, 8, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 20, 22, 22, 22, 22},
};
static const uint8_t EG_INC[17][8] = {  /* envelope steps: rows by rate % 4 below 48, then by rate */
    {0, 1, 0, 1, 0, 1, 0, 1}, {0, 1, 0, 1, 1, 1, 0, 1}, {0, 1, 1, 1, 0, 1, 1, 1}, {0, 1, 1, 1, 1, 1, 1, 1},
    {1, 1, 1, 1, 1, 1, 1, 1}, {1, 1, 1, 2, 1, 1, 1, 2}, {1, 2, 1, 2, 1, 2, 1, 2}, {1, 2, 2, 2, 1, 2, 2, 2},
    {2, 2, 2, 2, 2, 2, 2, 2}, {2, 2, 2, 4, 2, 2, 2, 4}, {2, 4, 2, 4, 2, 4, 2, 4}, {2, 4, 4, 4, 2, 4, 4, 4},
    {4, 4, 4, 4, 4, 4, 4, 4}, {4, 4, 4, 8, 4, 4, 4, 8}, {4, 8, 4, 8, 4, 8, 4, 8}, {4, 8, 8, 8, 4, 8, 8, 8},
    {8, 8, 8, 8, 8, 8, 8, 8},
};

int fm_detune(int dt, int kc) {
    int d = DT[dt & 3][kc & 31];
    return dt & 4 ? -d : d;
}

void fm_key(FmOp *o, int on) {
    if (on && !o->key) {
        o->phase = 0;
        o->state = FM_ATTACK;
    } else if (!on && o->key) {
        o->state = FM_RELEASE;
    }
    o->key = (uint8_t)on;
}

void fm_eg_step(FmOp *o, int kc, uint32_t cnt) {
    int rate = o->state == FM_ATTACK ? o->ar : o->state == FM_DECAY ? o->dr : o->state == FM_SUSTAIN ? o->sr : o->rr * 2 + 1;
    int r = rate ? 2 * rate + (kc >> (3 - o->ks)) : 0;
    if (r > 63) r = 63;
    int shift = r < 48 ? 11 - (r >> 2) : 0;
    if (!r || cnt & ((1u << shift) - 1)) return;
    int inc = EG_INC[r < 48 ? r & 3 : (r - 44 < 16 ? r - 44 : 16)][cnt >> shift & 7];
    int att = o->att;
    if (o->state == FM_ATTACK) {
        att = r >= 62 ? 0 : att + ((~att * inc) >> 4);
        if (att <= 0) att = 0, o->state = FM_DECAY;
    } else {
        att += inc;
        int sl = o->sl == 15 ? 0x3E0 : o->sl << 5;
        if (o->state == FM_DECAY && att >= sl) o->state = FM_SUSTAIN;
        if (att > 0x3FF) att = 0x3FF;
    }
    o->att = (uint16_t)att;
}

/* One operator: phase (10 bits) plus modulation, through log-sin and exp; +-8168. */
int fm_op_out(const FmOp *o, int mod, int am) {
    int env = o->att + (o->tl << 3) + am;
    if (env > 0x3FF) env = 0x3FF;
    unsigned p = ((o->phase >> 10) + (unsigned)mod) & 0x3FF;
    int q = p & 0xFF;
    if (p & 0x100) q ^= 0xFF;
    int level = LOGSIN[q] + (env << 2);
    int v = level >= 0x1FFF ? 0 : ((EXP[(level & 0xFF) ^ 0xFF] | 0x400) << 2) >> (level >> 8);
    return p & 0x200 ? -v : v;
}

int fm_algorithm(FmOp op[4], int alg, int fb, int16_t fb_out[2], const int am[4]) {
    int a1 = am[0], a2 = am[1], a3 = am[2], a4 = am[3];
    int f = fb ? (fb_out[0] + fb_out[1]) >> (10 - fb) : 0;
    int s1 = fm_op_out(&op[0], f, a1), s2, s3, out;
    fb_out[1] = fb_out[0], fb_out[0] = (int16_t)s1;
    switch (alg) {
    case 0: s2 = fm_op_out(&op[1], s1 >> 1, a2); s3 = fm_op_out(&op[2], s2 >> 1, a3); out = fm_op_out(&op[3], s3 >> 1, a4); break;
    case 1: s2 = fm_op_out(&op[1], 0, a2); s3 = fm_op_out(&op[2], (s1 + s2) >> 1, a3); out = fm_op_out(&op[3], s3 >> 1, a4); break;
    case 2: s2 = fm_op_out(&op[1], 0, a2); s3 = fm_op_out(&op[2], s2 >> 1, a3); out = fm_op_out(&op[3], (s1 + s3) >> 1, a4); break;
    case 3: s2 = fm_op_out(&op[1], s1 >> 1, a2); s3 = fm_op_out(&op[2], 0, a3); out = fm_op_out(&op[3], (s2 + s3) >> 1, a4); break;
    case 4: s2 = fm_op_out(&op[1], s1 >> 1, a2); s3 = fm_op_out(&op[2], 0, a3); out = s2 + fm_op_out(&op[3], s3 >> 1, a4); break;
    case 5: s2 = fm_op_out(&op[1], s1 >> 1, a2); s3 = fm_op_out(&op[2], s1 >> 1, a3); out = s2 + s3 + fm_op_out(&op[3], s1 >> 1, a4); break;
    case 6: s2 = fm_op_out(&op[1], s1 >> 1, a2); s3 = fm_op_out(&op[2], 0, a3); out = s2 + s3 + fm_op_out(&op[3], 0, a4); break;
    default: s2 = fm_op_out(&op[1], 0, a2); s3 = fm_op_out(&op[2], 0, a3); out = s1 + s2 + s3 + fm_op_out(&op[3], 0, a4); break;
    }
    return out > 8191 ? 8191 : out < -8192 ? -8192 : out;
}
