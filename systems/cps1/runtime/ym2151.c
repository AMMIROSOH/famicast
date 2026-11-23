/* The YM2151 (ym2151.h). Registers:
 *   $01 bit 1 LFO reset; $08 key on (bits 0-2 channel, 3-6 M1 C1 M2 C2);
 *   $0F noise; $10/$11 timer A (10 bits), $12 timer B; $14 timer control
 *   (bits 0-1 load, 2-3 IRQ enable, 4-5 flag reset); $18 LFO frequency;
 *   $19 AMD / PMD (bit 7); $1B LFO waveform (bits 0-1).
 *   $20+ch R L FB CON; $28+ch key code; $30+ch key fraction; $38+ch PMS AMS.
 *   $40-$FF per operator, slots M1 M2 C1 C2 (8 apart): DT1 MUL, TL, KS AR,
 *   AM-enable D1R, DT2 D2R, D1L RR.
 * The operator and envelope are core/fm.c's. Sources: docs/sources.md (CPS-1 sound). */
#include <math.h>
#include <string.h>

#include "ym2151.h"

static const uint8_t SLOT_OP[4] = {0, 2, 1, 3};          /* slot M1, M2, C1, C2 to S1-S4 */
static const uint8_t NOTE[16] = {0, 1, 2, 2, 3, 4, 5, 5, 6, 7, 8, 8, 9, 10, 11, 11};  /* key code note: C# first */
static const uint16_t DT2[4] = {0, 384, 500, 608};      /* 0, 600, 781, 950 cents in 1/64 semitones */
static const uint16_t PM_CENTS[8] = {0, 5, 10, 20, 50, 100, 400, 700};
static uint32_t INC[768];                                /* phase increment x 64 for octave 0, by 1/64 semitone */

/* shortcut: increments computed from 440 Hz at key code $4A, not the chip's
 * table; the LFO rate as a smooth curve; noise as a 17-bit LFSR at
 * 32 - frequency samples a step. Upgrade if a game sounds off. */
static void tables(void) {
    if (INC[0]) return;
    fm_tables();
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
        if (v & 2) y->lfo_phase = 0;
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

/* the LFO's value now: unipolar 0-255 for AM, bipolar -128..127 for PM */
static void lfo(Opm *y, int *am, int *pm) {
    unsigned p = y->lfo_phase >> 16 & 0xFF;  /* 0-255 through a cycle */
    int u, b;
    switch (y->wave) {
    case 0: u = 255 - (int)p, b = (int)p - 128; break;                                  /* saw */
    case 1: u = p < 128 ? 255 : 0, b = p < 128 ? 127 : -128; break;                     /* square */
    case 2: u = p < 128 ? 255 - (int)p * 2 : ((int)p - 128) * 2;                        /* triangle */
            b = p < 64 ? (int)p * 2 : p < 192 ? 255 - (int)p * 2 : (int)p * 2 - 512; break;
    default: u = y->lfo_rand, b = (int)y->lfo_rand - 128; break;                        /* noise */
    }
    *am = u * y->amd >> 7;
    *pm = b * y->pmd >> 7;
    /* about 0.008 Hz at 0 to 53 Hz at 255 */
    double hz = 0.0081 * pow(2, y->lfo_freq * 12.67 / 255);
    uint32_t before = y->lfo_phase;
    y->lfo_phase = (y->lfo_phase + (uint32_t)(hz * (1 << 24) / OPM_RATE)) & 0xFFFFFF;
    if ((y->lfo_phase ^ before) & 0xFF0000) y->lfo_rand = (uint8_t)(y->noise_lfsr * 73 >> 3);
}

static int channel(Opm *y, int c, int lfo_am, int lfo_pm) {
    OpmCh *ch = &y->ch[c];
    int octave = ch->kc >> 4 & 7, kc5 = ch->kc >> 2 & 31;
    int base = (octave * 12 + NOTE[ch->kc & 15]) * 64 + ch->kf;
    if (ch->pms) base += lfo_pm * PM_CENTS[ch->pms] * 64 / 100 / 128;
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
    int am = ch->ams ? lfo_am >> (3 - ch->ams) : 0;
    int am4[4] = {ch->op[0].am ? am : 0, ch->op[1].am ? am : 0, ch->op[2].am ? am : 0, ch->op[3].am ? am : 0};
    if (c == 7 && y->noise & 0x80) {  /* noise in place of C2 */
        FmOp n = ch->op[3];
        FmOp silent = n;
        silent.tl = 127, silent.att = 0x3FF;
        ch->op[3] = silent;
        int out = fm_algorithm(ch->op, ch->con, ch->fb, ch->fb_out, am4);
        ch->op[3] = n;
        n.phase = 0x100 << 10;  /* the operator's peak, signed by the noise */
        int level = fm_op_out(&n, 0, am4[3]);
        out += y->noise_lfsr & 1 ? level : -level;
        return out > 8191 ? 8191 : out < -8192 ? -8192 : out;
    }
    return fm_algorithm(ch->op, ch->con, ch->fb, ch->fb_out, am4);
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
    if (++y->noise_cnt >= 32u - (y->noise & 31)) {
        y->noise_cnt = 0;
        uint32_t bit = (y->noise_lfsr ^ y->noise_lfsr >> 3) & 1;
        y->noise_lfsr = y->noise_lfsr >> 1 | bit << 16;
    }
    int sum = 0;
    for (int c = 0; c < 8; c++) {
        int out = channel(y, c, lfo_am, lfo_pm);
        sum += (y->ch[c].pan & 1 ? out : 0) + (y->ch[c].pan & 2 ? out : 0);
    }
    if (++y->eg_sub >= 3) y->eg_sub = 0, y->eg_cnt++;  /* the envelopes step every third sample */
    timers(y);
    sum /= 2;
    return sum > 16383 ? 16383 : sum < -16384 ? -16384 : sum;
}
