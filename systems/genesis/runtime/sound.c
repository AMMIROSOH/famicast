/* The PSG and the YM2612 (sound.h). Sources, from memory: the SN76489
 * pages of SMS Power (Sega's 16-bit LFSR with taps 0 and 3, 2 dB volume
 * steps), the YM2612 notes of the SpritesMind forum and Nemesis, MAME's
 * fm.c tables (detune, envelope increments, LFO periods, AM depths) and
 * the log-sin / exp operator of the OPN family as Nuked-OPN2 models it
 * (docs/sources.md). Checked by systems/genesis/tests/md_tests.c, not against a console. */
#include <math.h>
#include <string.h>

#include "fm.h"
#include "md.h"

static int32_t PM_MUL[8][8];           /* F-number change x 65536 for each FMS and LFO PM step 0-7 */
static float PSG_VOL[16];
static int tables_done;

static void tables(void) {
    if (tables_done) return;
    /* FMS: the deepest vibrato in cents; the LFO's PM is a triangle of 8 steps a quarter.
     * shortcut: a smooth scale, not the chip's table of F-number bits */
    static const double CENTS[8] = {0, 3.4, 6.7, 10, 14, 20, 40, 80};
    for (int f = 0; f < 8; f++)
        for (int v = 0; v < 8; v++) PM_MUL[f][v] = (int32_t)lround((exp2(CENTS[f] * v / 7 / 1200) - 1) * 65536);
    for (int i = 0; i < 15; i++) PSG_VOL[i] = (float)pow(10, -0.1 * i);  /* 2 dB a step */
    PSG_VOL[15] = 0;
    tables_done = 1;
}

/* ---- the YM2612 ---- */
static const uint8_t LFO_PERIOD[8] = {108, 77, 71, 67, 62, 44, 8, 5};  /* samples per LFO step (128 a cycle) */
static const uint8_t AM_SHIFT[4] = {8, 3, 1, 0};  /* AMS: 0, 1.4, 5.9, 11.8 dB */
static const uint8_t SLOT_OP[4] = {0, 2, 1, 3};   /* register slot (+0, +4, +8, +12) to S1..S4 */

static int key_code(int fnum, int block) {
    int f11 = fnum >> 10 & 1, f10 = fnum >> 9 & 1, f9 = fnum >> 8 & 1, f8 = fnum >> 7 & 1;
    return block << 2 | f11 << 1 | (f11 ? (f10 | f9 | f8) : (f10 & f9 & f8));
}

void ym_reset(Ym *y) {
    memset(y, 0, sizeof *y);
    for (int c = 0; c < 6; c++) {
        y->ch[c].pan = 3;
        for (int o = 0; o < 4; o++) y->ch[c].op[o].state = FM_RELEASE, y->ch[c].op[o].att = 0x3FF;
    }
}

static int channel(Ym *y, int c) {
    YmCh *ch = &y->ch[c];
    int lfo_am = !y->lfo_on ? 0 : y->lfo_step < 64 ? (y->lfo_step ^ 63) << 1 : (y->lfo_step & 63) << 1;
    int am = lfo_am >> AM_SHIFT[ch->ams];
    for (int i = 0; i < 4; i++) {  /* phases */
        FmOp *o = &ch->op[i];
        int fn = ch->fnum, bl = ch->block;
        if (c == 2 && (y->mode & 0xC0) && i < 3) {  /* channel 3's special mode: S1-S3 have their own */
            static const int IX[3] = {1, 2, 0};
            fn = y->fnum3[IX[i]], bl = y->block3[IX[i]];
        }
        int kc = key_code(fn, bl);
        if (y->lfo_on && ch->fms) {
            int s = y->lfo_step >> 2 & 31, v = s & 8 ? 7 - (s & 7) : s & 7;
            int d = (int)(((int64_t)fn * PM_MUL[ch->fms][v]) >> 16);
            fn += s & 16 ? -d : d;
        }
        uint32_t inc = ((uint32_t)((fn << bl) >> 1) + (uint32_t)fm_detune(o->dt, kc)) & 0x1FFFF;
        inc = o->mul ? inc * o->mul : inc >> 1;
        o->phase = (o->phase + inc) & 0xFFFFF;
        if (y->eg_sub == 0) fm_eg_step(o, kc, y->eg_cnt);
    }
    int am4[4] = {ch->op[0].am ? am : 0, ch->op[1].am ? am : 0, ch->op[2].am ? am : 0, ch->op[3].am ? am : 0};
    return fm_algorithm(ch->op, ch->alg, ch->fb, ch->fb_out, am4);
}

static void ym_sample(Ym *y, float *l, float *r) {
    if ((y->mode & 1) && ++y->ta_cnt >= 1024) {  /* shortcut: CSM key-on at timer A is not modelled */
        y->ta_cnt = y->ta;
        if (y->mode & 4) y->status |= 1;
    }
    if ((y->mode & 2) && ++y->tb_sub >= 16) {
        y->tb_sub = 0;
        if (++y->tb_cnt >= 256) {
            y->tb_cnt = y->tb;
            if (y->mode & 8) y->status |= 2;
        }
    }
    if (y->lfo_on && ++y->lfo_cnt >= LFO_PERIOD[y->lfo_freq]) y->lfo_cnt = 0, y->lfo_step = (y->lfo_step + 1) & 127;
    if (++y->eg_sub >= 3) y->eg_sub = 0, y->eg_cnt++;  /* the envelopes step every third sample */
    for (int c = 0; c < 6; c++) {
        int out = channel(y, c);
        if (c == 5 && y->dac_on) out = (y->dac - 128) << 6;
        float v = (float)out / (8192.0f * 3);  /* one channel at full: a third of full scale */
        if (y->ch[c].pan & 2) *l += v;
        if (y->ch[c].pan & 1) *r += v;
    }
}

static void ym_reg(Ym *y, int port, int reg, uint8_t v) {
    if (reg < 0x30) {
        if (port) return;  /* the global registers are on port 0 only */
        switch (reg) {
        case 0x22: y->lfo_on = v >> 3 & 1, y->lfo_freq = v & 7; if (!y->lfo_on) y->lfo_step = 0, y->lfo_cnt = 0; break;
        case 0x24: y->ta = (uint16_t)((y->ta & 3) | v << 2); break;
        case 0x25: y->ta = (uint16_t)((y->ta & 0x3FC) | (v & 3)); break;
        case 0x26: y->tb = v; break;
        case 0x27:
            if ((v & 1) && !(y->mode & 1)) y->ta_cnt = y->ta;
            if ((v & 2) && !(y->mode & 2)) y->tb_cnt = y->tb, y->tb_sub = 0;
            y->status &= (uint8_t)~(v >> 4 & 3);
            y->mode = v;
            break;
        case 0x28: {
            int c = v & 7;
            if ((c & 3) == 3) break;
            YmCh *ch = &y->ch[(c & 3) + (c & 4 ? 3 : 0)];
            for (int i = 0; i < 4; i++) fm_key(&ch->op[i], v >> (4 + i) & 1);
            break;
        }
        case 0x2A: y->dac = v; break;
        case 0x2B: y->dac_on = v >> 7; break;
        }
        return;
    }
    int c = reg & 3;
    if (reg < 0xA0) {
        if (c == 3) return;
        FmOp *o = &y->ch[port * 3 + c].op[SLOT_OP[reg >> 2 & 3]];
        switch (reg & 0xF0) {
        case 0x30: o->dt = v >> 4 & 7, o->mul = v & 15; break;
        case 0x40: o->tl = v & 0x7F; break;
        case 0x50: o->ks = v >> 6, o->ar = v & 31; break;
        case 0x60: o->am = v >> 7, o->dr = v & 31; break;
        case 0x70: o->sr = v & 31; break;
        case 0x80: o->sl = v >> 4, o->rr = v & 15; break;
        }  /* $90: SSG-EG. shortcut: not modelled (few games use it) */
        return;
    }
    if (reg >= 0xA8 && reg < 0xB0) {
        if (port || c == 3) return;
        if (reg < 0xAC) y->fnum3[c] = (uint16_t)((y->fhi3 & 7) << 8 | v), y->block3[c] = y->fhi3 >> 3 & 7;
        else y->fhi3 = v & 0x3F;
        return;
    }
    if (c == 3) return;
    YmCh *ch = &y->ch[port * 3 + c];
    switch (reg & 0xFC) {
    case 0xA0: ch->fnum = (uint16_t)((y->fhi & 7) << 8 | v), ch->block = y->fhi >> 3 & 7; break;
    case 0xA4: y->fhi = v & 0x3F; break;
    case 0xB0: ch->fb = v >> 3 & 7, ch->alg = v & 7; break;
    case 0xB4: ch->pan = v >> 6, ch->ams = v >> 4 & 3, ch->fms = v & 7; break;
    }
}

/* ---- the PSG ---- */
static void psg_step(Psg *p) {
    for (int i = 0; i < 3; i++)
        if (--p->count[i] == 0 || p->count[i] > 0x3FF) {  /* period 0 counts as 1 */
            p->count[i] = p->period[i] ? p->period[i] : 1;
            p->out[i] ^= 1;
        }
    if (--p->count[3] == 0 || p->count[3] > 0x3FF) {
        p->count[3] = (p->noise & 3) == 3 ? (p->period[2] ? p->period[2] : 1) : (uint16_t)(0x10 << (p->noise & 3));
        if ((p->out[3] ^= 1)) {  /* the LFSR shifts on the flip-flop's rise */
            int fb = p->noise & 4 ? (p->lfsr ^ p->lfsr >> 3) & 1 : p->lfsr & 1;
            p->lfsr = (uint16_t)(p->lfsr >> 1 | fb << 15);
        }
    }
}
static float psg_level(const Psg *p) {
    float v = 0;
    for (int i = 0; i < 3; i++) v += p->out[i] ? PSG_VOL[p->vol[i]] : -PSG_VOL[p->vol[i]];
    v += p->lfsr & 1 ? PSG_VOL[p->vol[3]] : -PSG_VOL[p->vol[3]];
    return v * 0.15f;  /* shortcut: the PSG's level against the FM channels is an estimate */
}

void psg_write(Md *m, uint64_t now, uint8_t v) {
    snd_run(m, now);
    Psg *p = &m->snd.psg;
    if (v & 0x80) p->latch = v >> 4 & 7;
    int r = p->latch, ch = r >> 1;
    if (r & 1) p->vol[ch] = v & 15;
    else if (ch == 3) p->noise = v & 7, p->lfsr = 0x8000;
    else if (v & 0x80) p->period[ch] = (uint16_t)((p->period[ch] & 0x3F0) | (v & 15));
    else p->period[ch] = (uint16_t)((p->period[ch] & 15) | (v & 0x3F) << 4);
}

/* ---- the timeline ---- */
void snd_reset(Sound *s) {
    tables();
    memset(s, 0, sizeof *s);
    ym_reset(&s->ym);
    for (int i = 0; i < 4; i++) s->psg.vol[i] = 15, s->psg.count[i] = 1;
    s->psg.lfsr = 0x8000;
    s->next = SND_SAMPLE_CLOCKS;
}

void snd_run(Md *m, uint64_t now) {
    Sound *s = &m->snd;
    while (s->next <= now) {
        float l = 0, r = 0, psg = 0;
        int steps = 0;
        for (s->psg.frac += SND_SAMPLE_CLOCKS; s->psg.frac >= 240; s->psg.frac -= 240, steps++) {  /* 16 PSG clocks */
            psg_step(&s->psg);
            psg += psg_level(&s->psg);
        }
        if (steps) psg /= (float)steps;
        ym_sample(&s->ym, &l, &r);
        float in[2] = {l + psg, r + psg};
        for (int k = 0; k < 2; k++) {  /* a one-pole high-pass at about 8 Hz */
            float y = in[k] - s->dc_in[k] + 0.999f * s->dc_out[k];
            s->dc_in[k] = in[k], s->dc_out[k] = y;
            if (s->n < SND_BUF) s->buf[2 * s->n + k] = y > 1 ? 1 : y < -1 ? -1 : y;
        }
        if (s->n < SND_BUF) s->n++;
        s->next += SND_SAMPLE_CLOCKS;
    }
}

uint8_t ym_read(Md *m, uint64_t now) {
    snd_run(m, now);
    return m->snd.ym.status;  /* shortcut: never busy */
}

void ym_write(Md *m, uint64_t now, int a, uint8_t v) {
    snd_run(m, now);
    Ym *y = &m->snd.ym;
    if (!(a & 1)) y->addr[a >> 1] = v;
    else ym_reg(y, a >> 1, y->addr[a >> 1], v);
}

int snd_take(Sound *s, float *out, int max) {
    int n = s->n < max ? s->n : max;
    memcpy(out, s->buf, (size_t)n * 2 * sizeof *out);
    memmove(s->buf, s->buf + 2 * n, (size_t)(s->n - n) * 2 * sizeof *out);
    s->n -= n;
    return n;
}
