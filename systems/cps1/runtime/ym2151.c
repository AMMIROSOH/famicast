/* The YM2151 (ym2151.h). Registers:
 *   $01 bit 1 LFO reset; $08 key on (bits 0-2 channel, 3-6 M1 C1 M2 C2);
 *   $0F noise; $10/$11 timer A (10 bits), $12 timer B; $14 timer control
 *   (bits 0-1 load, 2-3 IRQ enable, 4-5 flag reset); $18 LFO frequency;
 *   $19 AMD / PMD (bit 7); $1B LFO waveform (bits 0-1).
 *   $20+ch R L FB CON; $28+ch key code; $30+ch key fraction; $38+ch PMS AMS.
 *   $40-$FF per operator, slots M1 M2 C1 C2 (8 apart): DT1 MUL, TL, KS AR,
 *   AM-enable D1R, DT2 D2R, D1L RR.
 * The operator and envelope are core/audio/fm.c's. Sources: docs/sources.md (CPS-1 sound). */
#include <math.h>
#include <string.h>

#include "ym2151.h"

static const uint8_t SLOT_OP[4] = {0, 2, 1, 3};          /* slot M1, M2, C1, C2 to S1-S4 */
static const uint8_t NOTE[16] = {0, 1, 2, 2, 3, 4, 5, 5, 6, 7, 8, 8, 9, 10, 11, 11};  /* key code note: C# first */
static const uint16_t DT2[4] = {0, 384, 500, 608};      /* 0, 600, 781, 950 cents in 1/64 semitones */
static uint32_t INC[768];                                /* phase increment x 64 for octave 0, by 1/64 semitone */

/* Increments computed from 440 Hz at key code $4A; within 1.2 cents of
 * ymfm's YM2151 on every key code (docs/sources.md). */
static void tables(void) {
    if (INC[0]) return;
    for (int i = 0; i < 768; i++) {
        double semis = i / 64.0 - (4 * 12 + 8);        /* from A4: octave 4, note A (index 8) */
        INC[i] = (uint32_t)lround(440.0 * pow(2, semis / 12) * (1 << 20) / OPM_RATE * 64);
    }
}

void opm_reset(Opm *y) {
    tables();
    memset(y, 0, sizeof *y);
    for (int c = 0; c < 8; c++)
        for (int o = 0; o < 4; o++) y->ch[c].op[o].state = FM_RELEASE, y->ch[c].op[o].att = 0x3FF;
    y->noise_lfsr = 1;
}

uint8_t opm_status(const Opm *y) { return y->status; }

static void update_irq(Opm *y) { y->irq = (uint8_t)((y->status & 1 && y->mode & 4) || (y->status & 2 && y->mode & 8)); }

static void reg(Opm *y, uint8_t r, uint8_t v) {
    if (r == 0x01) {
        y->test = v;
    } else if (r == 0x08) {
        OpmCh *ch = &y->ch[v & 7];
        for (int i = 0; i < 4; i++) fm_key(&ch->op[i], v >> (3 + i) & 1);
    } else if (r == 0x0F) {
        y->noise = v;
    } else if (r == 0x10) {
        y->ta = (uint16_t)((y->ta & 3) | v << 2);
    } else if (r == 0x11) {
        y->ta = (uint16_t)((y->ta & 0x3FC) | (v & 3));
    } else if (r == 0x12) {
        y->tb = v;
    } else if (r == 0x14) {
        if ((v & 1) && !(y->mode & 1)) y->ta_cnt = 0;  /* loading restarts the count */
        if ((v & 2) && !(y->mode & 2)) y->tb_cnt = 0;
        if (v & 0x10) y->status &= (uint8_t)~1;
        if (v & 0x20) y->status &= (uint8_t)~2;
        y->mode = v;
        update_irq(y);
    } else if (r == 0x18) {
        y->lfo_freq = v;
    } else if (r == 0x19) {
        if (v & 0x80) y->pmd = v & 0x7F;
        else y->amd = v & 0x7F;
    } else if (r == 0x1B) {
        y->wave = v & 3;
    } else if (r >= 0x20 && r < 0x40) {
        OpmCh *ch = &y->ch[r & 7];
        switch (r & 0x38) {
        case 0x20: ch->pan = v >> 6, ch->fb = v >> 3 & 7, ch->con = v & 7; break;
        case 0x28: ch->kc = v & 0x7F; break;
        case 0x30: ch->kf = v >> 2; break;
        default: ch->pms = v >> 4 & 7, ch->ams = v & 3; break;
        }
    } else if (r >= 0x40) {
        OpmCh *ch = &y->ch[r & 7];
        int i = SLOT_OP[r >> 3 & 3];
        FmOp *o = &ch->op[i];
        switch (r & 0xE0) {
        case 0x40: o->dt = v >> 4 & 7, o->mul = v & 15; break;
        case 0x60: o->tl = v & 0x7F; break;
        case 0x80: o->ks = v >> 6, o->ar = v & 31; break;
        case 0xA0: o->am = v >> 7, o->dr = v & 31; break;
        case 0xC0: ch->dt2[i] = v >> 6, o->sr = v & 31; break;
        default: o->sl = v >> 4, o->rr = v & 15; break;
        }
    }
}

void opm_write(Opm *y, int port, uint8_t v) {
    if (port == 0) y->addr = v;
    else reg(y, y->addr, v);
}

/* The LFO, one step a sample: a counter advancing by (16 + LFRQ's low 4
 * bits) << its high 4 bits, its bits 22-29 the position in the waveform.
 * AM is unsigned 0-255, PM signed; both are scaled by their depth / 128.
 * The noise waveform takes the noise LFSR's bits 17-24. */
static void lfo(Opm *y, int *am, int *pm) {
    y->lfo_phase += (uint32_t)(16 | (y->lfo_freq & 15)) << (y->lfo_freq >> 4);
    if (y->test & 2) y->lfo_phase = 0;  /* held in reset */
    unsigned i = y->lfo_phase >> 22 & 0xFF;
    int u, b;
    switch (y->wave) {
    case 0: u = (int)(i ^ 0xFF), b = (int8_t)i; break;                     /* saw */
    case 1: u = i & 0x80 ? 0 : 0xFF, b = (int8_t)(u ^ 0x80); break;         /* square */
    case 2: u = (int)(i & 0x80 ? i << 1 : (i ^ 0xFF) << 1) & 0xFF;          /* triangle */
            b = (int8_t)(i & 0x40 ? u : ~u); break;
    default: u = (int)(y->noise_lfsr >> 17 & 0xFF), b = (int8_t)u; break;  /* noise */
    }
    *am = u * y->amd >> 7;
    *pm = b * y->pmd >> 7;
}

static int channel(Opm *y, int c, int lfo_am, int lfo_pm) {
    OpmCh *ch = &y->ch[c];
    int octave = ch->kc >> 4 & 7, kc5 = ch->kc >> 2 & 31;
    int base = (octave * 12 + NOTE[ch->kc & 15]) * 64 + ch->kf;
    if (ch->pms) base += ch->pms < 6 ? lfo_pm >> (6 - ch->pms) : lfo_pm * (1 << (ch->pms - 5));  /* 1/64 semitones */
    for (int i = 0; i < 4; i++) {
        FmOp *o = &ch->op[i];
        int n = base + DT2[ch->dt2[i]];
        if (n < 0) n = 0;
        uint32_t inc = (INC[n % 768] << (n / 768)) >> 6;
        inc = (inc + (uint32_t)fm_detune(o->dt, kc5)) & 0xFFFFF;
        inc = o->mul ? inc * o->mul : inc >> 1;
        o->phase = (o->phase + inc) & 0xFFFFF;
        if (y->eg_sub == 0) fm_eg_step(o, kc5, y->eg_cnt);
    }
    int am = ch->ams ? lfo_am << (ch->ams - 1) : 0;  /* up to 95.6 dB */
    int am4[4] = {ch->op[0].am ? am : 0, ch->op[1].am ? am : 0, ch->op[2].am ? am : 0, ch->op[3].am ? am : 0};
    if (c == 7 && y->noise & 0x80) {  /* noise in place of C2 */
        FmOp n = ch->op[3];
        FmOp silent = n;
        silent.tl = 127, silent.att = 0x3FF;
        ch->op[3] = silent;
        int out = fm_algorithm(ch->op, ch->con, ch->fb, ch->fb_out, am4, 32767);
        ch->op[3] = n;
        int att = n.att + (n.tl << 3) + am4[3];  /* the envelope, inverted, with no sine */
        int level = ((att > 0x3FF ? 0x3FF : att) ^ 0x3FF) << 1;
        out += y->noise_state ? -level : level;
        return out;
    }
    return fm_algorithm(ch->op, ch->con, ch->fb, ch->fb_out, am4, 32767);
}

static void timers(Opm *y) {
    if (y->mode & 1 && ++y->ta_cnt >= 1024 - y->ta) {
        y->ta_cnt = 0;
        if (y->mode & 4) y->status |= 1;
    }
    if (y->mode & 2 && ++y->tb_cnt >= 16 * (256 - y->tb)) {
        y->tb_cnt = 0;
        if (y->mode & 8) y->status |= 2;
    }
    update_irq(y);
}

int opm_sample(Opm *y) {
    int lfo_am, lfo_pm;
    lfo(y, &lfo_am, &lfo_pm);
    for (int k = 0; k < 2; k++) {  /* the noise LFSR steps twice a sample, latched at its frequency */
        y->noise_lfsr <<= 1;  /* the new bit from the shifted register's bits 17 and 14 */
        y->noise_lfsr |= (y->noise_lfsr >> 17 ^ y->noise_lfsr >> 14 ^ 1) & 1;
        if (y->noise_cnt++ >= (uint32_t)((y->noise & 31) ^ 31)) y->noise_cnt = 0, y->noise_state = y->noise_lfsr >> 17 & 1;
    }
    int l = 0, r = 0;
    for (int c = 0; c < 8; c++) {
        int out = channel(y, c, lfo_am, lfo_pm);
        if (y->ch[c].pan & 1) l += out;
        if (y->ch[c].pan & 2) r += out;
    }
    if (++y->eg_sub >= 3) y->eg_sub = 0, y->eg_cnt++;  /* the envelopes step every third sample */
    timers(y);
    l = l > 32767 ? 32767 : l < -32768 ? -32768 : l;  /* each side 16 bits, as the YM3012 DAC takes it */
    r = r > 32767 ? 32767 : r < -32768 ? -32768 : r;
    return (l + r) / 2;
}
