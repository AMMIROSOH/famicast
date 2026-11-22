#include "apu.h"

#include <math.h>
#include <string.h>

static const uint8_t LENGTHS[32] = {10, 254, 20, 2,  40, 4,  80, 6,  160, 8,  60, 10, 14, 12, 26, 14,
                                    12, 16,  24, 18, 48, 20, 96, 22, 192, 24, 72, 26, 16, 28, 32, 30};
static const uint8_t DUTY[4][8] = {{0, 1, 0, 0, 0, 0, 0, 0}, {0, 1, 1, 0, 0, 0, 0, 0},
                                   {0, 1, 1, 1, 1, 0, 0, 0}, {1, 0, 0, 1, 1, 1, 1, 1}};
static const uint8_t TRIANGLE[32] = {15, 14, 13, 12, 11, 10, 9,  8,  7,  6,  5,  4,  3,  2,  1,  0,
                                     0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15};
/* Periods in CPU cycles, NTSC and PAL (nesdev "APU Noise", "APU DMC"; the
 * Dendy uses the NTSC tables) */
static const uint16_t NOISE_PERIOD[2][16] = {
    {4, 8, 16, 32, 64, 96, 128, 160, 202, 254, 380, 508, 762, 1016, 2034, 4068},
    {4, 8, 14, 30, 60, 88, 118, 148, 188, 236, 354, 472, 708, 944, 1890, 3778}};
static const uint16_t DMC_PERIOD[2][16] = {
    {428, 380, 340, 320, 286, 254, 226, 214, 190, 160, 142, 128, 106, 84, 72, 54},
    {398, 354, 316, 298, 276, 236, 210, 198, 176, 148, 132, 118, 98, 78, 66, 50}};
/* Frame counter steps in CPU cycles after a $4017 write (count 0): the
 * wiki's APU cycles x 2 + 3 ("APU Frame Counter"): 3728, 7456, 11185, the
 * IRQ at 14914 (GET), 18640 in mode 1; PAL 4156, 8313, 12469, 16626, 20782 */
static const int32_t FC_STEP[2][5] = {{7459, 14915, 22373, 29830, 37283}, {8315, 16629, 24941, 33254, 41567}};

static void mixer_tables(void);
static float PULSE_OUT[31], TND_OUT[203];  /* mixer tables, see mix() */
static float MMC5_PCM_STEP;                /* one step of the MMC5 DAC, see mix() */
static float S5B_LEVEL[32];                /* Sunsoft 5B: 5-bit level -> amplitude, see mix() */
static uint16_t LOGSIN[256], EXP[256];     /* VRC7: quarter sine as -log2 * 256; 2^x fraction * 1024 */
/* $4017 writes take effect on even APU clocks: one cycle later when written
 * on the other parity (blargg 2005 "Clock Jitter"; parity chosen by test). */
enum { FC_JITTER_PARITY = 1 };

static void quarter_frame(Apu *a);
static void half_frame(Apu *a);

void apu_reset(Apu *a, Cart *cart, int power_on) {
    if (power_on) {
        if (TND_OUT[1] == 0) mixer_tables();
        memset(a, 0, sizeof *a);
        a->pal = cart && cart->region == REGION_PAL;
        a->cpu_hz = !cart || cart->region == REGION_NTSC ? 1789773 : a->pal ? 1662607 : 1773448;
        a->noise.lfsr = 1;
        a->noise.period = a->noise.timer = NOISE_PERIOD[a->pal][0];
        a->pulse[0].ones_complement = 1;
        a->dmc.period = DMC_PERIOD[a->pal][0];
        a->dmc.timer = DMC_PERIOD[a->pal][0];
        a->dmc.bits = 8;
        a->dmc.silence = 1;
    }
    a->cart = cart;
    /* Namco 163: louder than the APU by a resistor on the board, so per game:
     * NES 2.0 submapper 3: 11-13 dB, 4: 16-17 dB, 5: 18-19.5 dB (the
     * loudest N163 square, 225 levels peak to peak, over the loudest APU
     * pulse, 0.1494); the middle of each, and 15 dB when not given.
     * Submappers 1 and 2: the sound is not wired out (the chip still runs). */
    a->has_mmc5 = cart && cart->mapper == 5;
    a->has_5b = cart && cart->mapper == 69;
    a->has_vrc6 = cart && (cart->mapper == 24 || cart->mapper == 26);
    a->has_vrc7 = cart && cart->mapper == 85 && cart->submapper != 1;
    a->has_fds = cart && cart->mapper == 20;
    if (power_on) a->fds.env_speed = 0xE8;  /* as the BIOS sets it */
    if (power_on)
        for (int i = 0; i < 6; i++) a->vrc7.op[i][0].env = a->vrc7.op[i][1].env = 127;
    if (power_on) a->vrc6.step[0] = a->vrc6.step[1] = 15;
    if (power_on) a->s5b.lfsr = 1;
    a->n163_gain = 0;
    if (cart && cart->n163_sound && cart->submapper != 1 && cart->submapper != 2) {
        /* 10^(dB/20) for 12, 16.5, 18.75 and 15 dB */
        float k = cart->submapper == 3 ? 3.98107f : cart->submapper == 4 ? 6.68344f : cart->submapper == 5 ? 8.65964f : 5.62341f;
        a->n163_gain = 0.1494f * k / 225.0f;
    }
    a->frame_irq = a->dmc_irq = 0;
    if (!power_on) {
        /* reset: $4015 cleared, but only pulse/noise length counters are
         * zeroed and unhalted; the triangle's is unaffected (apu_reset
         * len_ctrs_enabled; same as Mesen2) */
        for (int i = 0; i < 2; i++) a->pulse[i].halt = a->pulse[i].len = 0;
        a->noise.halt = a->noise.len = 0;
    }
    /* Power-up and reset act like writes of $00 to $4015 and of $00 (power) or
     * the mode last written (reset) to $4017, 9-12 cycles before the first
     * instruction (blargg apu_reset 4017_timing). Here the write lands at the
     * start of the 7-cycle CPU reset sequence; starting the counter at 2 makes
     * it 10 cycles (the test prints the delay; it was 8 - d for a delay d). */
    a->enabled = 0;
    a->dmc.remaining = 0;
    apu_write(a, 0x4017, a->fc_last & 0x80);  /* mode kept, IRQ inhibit cleared (as Mesen2) */
    a->fc_mode = a->fc_new;
    a->fc_delay = 0;
    a->fc_count = 2;
    if (a->fc_mode) {  /* mode 1's clock at count 1 has already happened */
        quarter_frame(a);
        half_frame(a);
    }
}

/* ---- clocks from the frame counter ---- */
static void clock_envelope(Envelope *e) {
    if (e->start) {
        e->start = 0;
        e->decay = 15;
        e->divider = e->period;
    } else if (e->divider == 0) {
        e->divider = e->period;
        if (e->decay) e->decay--;
        else if (e->loop) e->decay = 15;
    } else {
        e->divider--;
    }
}

static uint16_t sweep_target(const Pulse *p) {
    uint16_t change = p->period >> p->sweep_shift;
    if (!p->sweep_neg) return (uint16_t)(p->period + change);
    return (uint16_t)(p->period - change - (p->ones_complement ? 1 : 0));
}

static int pulse_muted(const Pulse *p) {
    return p->period < 8 || (!p->sweep_neg && sweep_target(p) > 0x7FF);
}

static void quarter_frame(Apu *a) {
    a->mix_dirty = 1;
    clock_envelope(&a->pulse[0].env);
    clock_envelope(&a->pulse[1].env);
    clock_envelope(&a->noise.env);
    Triangle *t = &a->tri;
    if (t->lin_reload) t->lin = t->lin_value;
    else if (t->lin) t->lin--;
    if (!t->control) t->lin_reload = 0;
}

static void half_frame(Apu *a) {
    a->mix_dirty = 1;
    a->last_half_clock = a->cycle;
    for (int i = 0; i < 2; i++) {
        Pulse *p = &a->pulse[i];
        p->len_before = p->len;
        if (p->len && !p->halt) p->len--;
        if (p->sweep_div == 0 && p->sweep_on && p->sweep_shift && !pulse_muted(p))
            p->period = sweep_target(p);
        if (p->sweep_div == 0 || p->sweep_reload) {
            p->sweep_div = p->sweep_period;
            p->sweep_reload = 0;
        } else {
            p->sweep_div--;
        }
    }
    a->tri.len_before = a->tri.len;
    if (a->tri.len && !a->tri.control) a->tri.len--;
    a->noise.len_before = a->noise.len;
    if (a->noise.len && !a->noise.halt) a->noise.len--;
}

/* Frame counter, one CPU cycle (blargg 2005 tables; count 0 = the write). */
static void frame_counter(Apu *a) {
    if (a->fc_delay && --a->fc_delay == 0) {
        a->fc_mode = a->fc_new;
        a->fc_count = 0;
        return;
    }
    int32_t c = ++a->fc_count;
    int irq = 0;
    const int32_t *s = FC_STEP[a->pal];
    if (c == s[0] || c == s[2]) quarter_frame(a);
    else if (c == s[1] || (a->fc_mode && c == 1)) { quarter_frame(a); half_frame(a); }
    else if (a->fc_mode == 0) {
        if (c == s[3]) irq = 1;
        else if (c == s[3] + 1) { quarter_frame(a); half_frame(a); irq = 1; }
        else if (c == s[3] + 2) { irq = 1; a->fc_count = 2; }
    } else if (c == s[4]) { quarter_frame(a); half_frame(a); a->fc_count = 1; }
    if (irq && !a->fc_inhibit) a->frame_irq = 1;
}

/* ---- channel timers, one CPU cycle ---- */
static void dmc_request_if_empty(Apu *a) {
    Dmc *d = &a->dmc;
    if (!d->buffer_full && d->remaining) d->dma_request = 1;
}

static void dmc_clock(Apu *a) {
    Dmc *d = &a->dmc;
    if (!d->silence) {
        if (d->shift & 1) { if (d->level <= 125) d->level += 2; }
        else if (d->level >= 2) d->level -= 2;
    }
    d->shift >>= 1;
    if (--d->bits == 0) {
        d->bits = 8;
        if (!d->buffer_full) {
            d->silence = 1;
        } else {
            d->silence = 0;
            d->shift = d->buffer;
            d->buffer_full = 0;
            dmc_request_if_empty(a);
        }
    }
}

/* a silent pulse or noise channel: its timer steps leave mix() unchanged */
static int env_volume(const Envelope *e) { return e->constant ? e->period : e->decay; }

static void timers(Apu *a) {
    if (a->cycle & 1) {  /* pulse timers run on APU cycles (every other CPU cycle) */
        for (int i = 0; i < 2; i++) {
            Pulse *p = &a->pulse[i];
            if (p->timer == 0) {
                p->timer = p->period;
                p->seq = (p->seq + 1) & 7;
                if (p->len && env_volume(&p->env)) a->mix_dirty = 1;
            }
            else p->timer--;
        }
    }
    Triangle *t = &a->tri;
    if (t->timer == 0) {
        t->timer = t->period;
        if (t->lin && t->len) t->seq = (t->seq + 1) & 31, a->mix_dirty = 1;
    } else {
        t->timer--;
    }
    Noise *n = &a->noise;
    if (--n->timer == 0) {
        n->timer = n->period;
        uint16_t fb = (n->lfsr ^ (n->lfsr >> (n->mode ? 6 : 1))) & 1;
        n->lfsr = (uint16_t)(n->lfsr >> 1 | fb << 14);
        if (n->len && env_volume(&n->env)) a->mix_dirty = 1;
    }
    Dmc *d = &a->dmc;
    if (--d->timer == 0) {
        d->timer = d->period;
        uint8_t level = d->level;
        dmc_clock(a);
        if (d->level != level) a->mix_dirty = 1;
    }
}

/* ---- mixer (nesdev "APU Mixer", lookup-table form) ----
 * Tables built once from the same float formulas, so the output is the same
 * bit for bit; mix() runs every CPU cycle. */

static void mixer_tables(void) {
    for (int p = 1; p < 31; p++) PULSE_OUT[p] = 95.52f / (8128.0f / (float)p + 100.0f);
    for (int t = 1; t < 203; t++) TND_OUT[t] = 163.67f / (24329.0f / (float)t + 100.0f);
    MMC5_PCM_STEP = TND_OUT[127] / 127.0f;
    /* 5B: 1.5 dB per level, levels 0 and 1 silent; level 31 (volume 15) is
     * about 8 dB over a full APU square (0.1494) */
    float v = 0.1494f * 2.51189f;
    for (int i = 31; i >= 2; i--, v *= 0.841395f) S5B_LEVEL[i] = v;
    for (int i = 0; i < 256; i++) {  /* the OPL/OPLL tables (integers: the same on every platform) */
        LOGSIN[i] = (uint16_t)lround(-log2(sin((i + 0.5) * 3.14159265358979 / 512)) * 256);
        EXP[i] = (uint16_t)lround((exp2(i / 256.0) - 1) * 1024);
    }
}

static float mix(Apu *a) {
    int p = 0;
    for (int i = 0; i < 2; i++) {
        const Pulse *q = &a->pulse[i];
        if (q->len && !pulse_muted(q) && DUTY[q->duty][q->seq])
            p += q->env.constant ? q->env.period : q->env.decay;
    }
    int t = TRIANGLE[a->tri.seq];
    const Noise *n = &a->noise;
    int nz = (n->len && !(n->lfsr & 1)) ? (n->env.constant ? n->env.period : n->env.decay) : 0;
    float out = PULSE_OUT[p] + TND_OUT[3 * t + 2 * nz + a->dmc.level];
    if (a->n163_gain != 0) out += a->n163_gain * cart_n163_level(a->cart);
    /* MMC5: pulses as loud as the APU's (the same table), the DAC like the
     * DMC with the same value (so up to twice as loud: 8 bits), all
     * inverted against the APU (nesdev "MMC5 audio") */
    if (a->has_mmc5) {
        int m = 0;
        for (int i = 0; i < 2; i++) {
            const Pulse *q = &a->mmc5.pulse[i];
            if (q->len && DUTY[q->duty][q->seq]) m += q->env.constant ? q->env.period : q->env.decay;
        }
        out -= PULSE_OUT[m] + MMC5_PCM_STEP * a->mmc5.pcm;
    }
    /* 5B: a channel sounds while its tone (or tone disabled) and its noise
     * (or noise disabled) are both high; volume bit 4 takes the envelope's
     * level, else volume v is level 2v + 1. Summed linearly; its signals
     * swing from rest to negative. shortcut: the amplifier compresses near
     * full volume (a ~2.6:1 knee per the nesdev forum measurements, not
     * settled); not modelled, upgrade if Gimmick! clips. */
    if (a->has_5b) {
        const Sunsoft5b *s = &a->s5b;
        float sum = 0;
        for (int i = 0; i < 3; i++) {
            int tone = (s->reg[7] >> i & 1) || s->tone_out[i], noise = (s->reg[7] >> (3 + i) & 1) || (s->lfsr & 1);
            int vol = s->reg[8 + i], level = (vol & 0x10) ? s->env_level : (vol & 15) ? (vol & 15) * 2 + 1 : 0;
            if (tone && noise) sum += S5B_LEVEL[level];
        }
        out -= sum;
    }
    /* VRC6: a linear 6-bit DAC over pulse + pulse + saw (0-61); a pulse at
     * volume 15 about as loud as an APU pulse at 15 (nesdev "VRC6 audio";
     * its polarity is not documented: not inverted here) */
    if (a->has_vrc6) {
        const Vrc6Audio *v = &a->vrc6;
        int level = 0;
        for (int i = 0; i < 2; i++) {
            uint8_t ctl = v->reg[i][0];
            if ((v->reg[i][2] & 0x80) && ((ctl & 0x80) || v->step[i] <= ((ctl >> 4) & 7))) level += ctl & 15;
        }
        if (v->reg[2][2] & 0x80) level += v->acc >> 3;
        out += PULSE_OUT[15] / 15.0f * (float)level;
    }
    /* VRC7: one channel at full level about 11 dB over an APU square at 15
     * (nesdev forum "VRC7 and 5B amplifier investigation", an estimate);
     * a carrier peaks at +-4084. shortcut: the amplifier's nonlinearity is
     * not modelled */
    if (a->has_vrc7) out += 0.1494f * 3.548f / 8168.0f * (float)a->vrc7.out;
    return out;
}

/* Box-filter to 48 kHz, then the NES's own output filters: high-pass 90 Hz
 * and 440 Hz, low-pass 14 kHz (nesdev "APU Mixer"). */
static void output(Apu *a) {
    if (a->mix_dirty) a->mix_last = mix(a), a->mix_dirty = 0;
    a->acc += a->mix_last;
    /* FDS: its ~2 kHz low-pass (1 pole) every cycle; full volume (63) about
     * 2.4x an APU square at 15, same polarity (nesdev "FDS audio") */
    if (a->has_fds) {
        a->fds.lp += 0.0069966f * ((float)a->fds.out - a->fds.lp);  /* 1 - e^(-2 pi 2000 / 1789773) */
        /* silent channel: the decay would stop on a denormal (x * 0.993 rounds
         * back to x), and denormal math every cycle made FDS games 2.5x slower */
        if (a->fds.lp < 1e-6f && a->fds.lp > -1e-6f) a->fds.lp = 0;
        a->acc += 2.4f * 0.1494f / 63.0f * a->fds.lp;
    }
    a->acc_n++;
    a->frac += APU_SAMPLE_RATE;
    if (a->frac < a->cpu_hz) return;
    a->frac -= a->cpu_hz;
    float x = a->acc / (float)a->acc_n;
    a->acc = 0;
    a->acc_n = 0;
    const float hp90 = 0.988356f, hp440 = 0.945541f, lp14k = 0.646946f;  /* 48 kHz coefficients */
    float y1 = hp90 * (a->hp1_y + x - a->hp1_x);
    a->hp1_x = x;
    a->hp1_y = y1;
    float y2 = hp440 * (a->hp2_y + y1 - a->hp2_x);
    a->hp2_x = y1;
    a->hp2_y = y2;
    a->lp_y += lp14k * (y2 - a->lp_y);
    if (a->buf_len < APU_BUF) a->buf[a->buf_len++] = a->lp_y;
}

/* MMC5 pulses: timers on APU cycles like the APU's; no sweep, and periods
 * under 8 are not muted. */
static void mmc5_clock(Apu *a) {
    Mmc5Audio *m = &a->mmc5;
    if (++m->frame >= 7424) {  /* M2 / 7424 = 241 Hz (die reading; not bench-tested) */
        m->frame = 0;
        for (int i = 0; i < 2; i++) {
            Pulse *p = &m->pulse[i];
            clock_envelope(&p->env);
            if (p->len && !p->halt) p->len--;
        }
        a->mix_dirty = 1;
    }
    if (a->cycle & 1) {
        for (int i = 0; i < 2; i++) {
            Pulse *p = &m->pulse[i];
            if (p->timer == 0) { p->timer = p->period; p->seq = (p->seq + 1) & 7; a->mix_dirty = 1; }
            else p->timer--;
        }
    }
}

/* 5B: every 16 CPU cycles, tone counters flip their square at the period
 * (Clock / (32 * period)), noise takes a new LFSR bit every second period
 * (Clock / (32 * period)), the envelope steps once a period (32 steps a
 * ramp). A period of 0 acts as 1. */
static void s5b_clock(Apu *a) {
    Sunsoft5b *s = &a->s5b;
    if (s->tick) { s->tick--; return; }
    s->tick = 15;
    for (int i = 0; i < 3; i++) {
        unsigned p = (unsigned)(s->reg[i * 2 + 1] & 0x0F) << 8 | s->reg[i * 2];
        if (++s->tone_count[i] >= (p ? p : 1)) s->tone_count[i] = 0, s->tone_out[i] ^= 1, a->mix_dirty = 1;
    }
    unsigned np = s->reg[6] & 0x1F;
    if (++s->noise_count >= (np ? np : 1)) {
        s->noise_count = 0;
        if ((s->noise_half ^= 1) == 0) {  /* 17-bit LFSR, taps 16 and 13 */
            s->lfsr = (s->lfsr >> 1) | (((s->lfsr ^ (s->lfsr >> 3)) & 1) << 16);
            a->mix_dirty = 1;
        }
    }
    unsigned ep = (unsigned)s->reg[0x0C] << 8 | s->reg[0x0B];
    if (++s->env_count >= (ep ? ep : 1)) {
        s->env_count = 0;
        if (!s->env_hold && ++s->env_step == 32) {  /* end of a ramp: shape bits C A a H */
            uint8_t shape = s->reg[0x0D];
            s->env_step = 0;
            if (!(shape & 8)) s->env_hold = 1, s->env_level = 0;
            else {
                if (shape & 2) s->env_attack ^= 1;  /* alternate */
                if (shape & 1) s->env_hold = 1, s->env_level = s->env_attack ? 31 : 0;
            }
        }
        if (!s->env_hold) s->env_level = s->env_attack ? s->env_step : (uint8_t)(31 - s->env_step);
        a->mix_dirty = 1;
    }
}

/* VRC6: each divider counts down every CPU cycle and clocks its channel
 * after period + 1 cycles: a pulse steps its duty (16 steps, so Clock /
 * (16 * (t + 1))); the saw adds its rate to the accumulator every second
 * clock and clears it on the 14th (Clock / (14 * (t + 1))). Disabled
 * channels hold (pulse at step 15, saw at 0). */
static void vrc6_clock(Apu *a) {
    Vrc6Audio *v = &a->vrc6;
    if (v->freq_ctrl & 1) return;  /* halt */
    int shift = (v->freq_ctrl & 4) ? 8 : (v->freq_ctrl & 2) ? 4 : 0;
    for (int i = 0; i < 3; i++) {
        if (!(v->reg[i][2] & 0x80)) continue;
        if (v->timer[i]) { v->timer[i]--; continue; }
        v->timer[i] = (uint16_t)((((v->reg[i][2] & 0x0F) << 8) | v->reg[i][1]) >> shift);
        if (i < 2) v->step[i] = (v->step[i] - 1) & 15;
        else if (++v->acc_clock == 14) v->acc = 0, v->acc_clock = 0;
        else if (!(v->acc_clock & 1)) v->acc = (uint8_t)(v->acc + (v->reg[2][0] & 0x3F));
        a->mix_dirty = 1;
    }
}

/* ---- FDS (nesdev "FDS audio"; the envelope, modulator and rounding as
 * Mesen2's FdsAudio and ModChannel, GPL-3 like this program) ---- */
static const uint8_t FDS_MASTER[4] = {36, 24, 17, 14};          /* full, 2/3, 2/4, 2/5 */
static const int8_t FDS_MOD_STEP[8] = {0, 1, 2, 4, 0, -4, -2, -1};  /* step 4 resets the counter */

static void fds_env_reset(const FdsAudio *f, FdsEnvelope *e) { e->timer = 8u * (e->speed + 1u) * f->env_speed; }

static int fds_env_tick(const FdsAudio *f, FdsEnvelope *e) {
    if (e->off || !f->env_speed || --e->timer) return 0;
    fds_env_reset(f, e);
    if (e->up && e->gain < 32) e->gain++;
    else if (!e->up && e->gain > 0) e->gain--;
    return 1;
}

static void fds_set_counter(FdsAudio *f, int v) {
    if (v >= 64) v -= 128;
    else if (v < -64) v += 128;
    f->mod_counter = (int8_t)v;
}

/* The pitch change: counter x gain, then x the wave's pitch, with the
 * hardware's odd rounding (nesdev "FDS audio", frequency modulation). */
static void fds_mod_update(FdsAudio *f) {
    int32_t t = f->mod_counter * f->mod.gain, rem = t & 0xF;
    t >>= 4;
    if (rem > 0 && !(t & 0x80)) t += f->mod_counter < 0 ? -1 : 2;
    if (t >= 192) t -= 256;
    else if (t < -64) t += 256;
    t *= f->vol.freq;
    rem = t & 0x3F;
    t >>= 6;
    f->mod_out = t + (rem >= 32);
}

static void fds_clock(Apu *a) {
    FdsAudio *f = &a->fds;
    if (!f->halt && !f->env_halt) {
        fds_env_tick(f, &f->vol);
        if (fds_env_tick(f, &f->mod)) fds_mod_update(f);
    }
    if (!f->mod_off && f->mod.freq) {
        f->mod_acc = (uint16_t)(f->mod_acc + f->mod.freq);
        if (f->mod_acc < f->mod.freq) {  /* overflow: the next modulator step */
            int step = f->mod_table[f->mod_pos];
            fds_set_counter(f, step == 4 ? 0 : f->mod_counter + FDS_MOD_STEP[step]);
            f->mod_pos = (f->mod_pos + 1) & 63;
            fds_mod_update(f);
        }
    }
    if (!f->wave_write) {  /* while the wave RAM is writable the output holds */
        int gain = f->vol.gain < 32 ? f->vol.gain : 32;
        f->out = (uint8_t)(f->wave[f->wave_pos] * gain * FDS_MASTER[f->master] / 1152);
    }
    int pitch = f->vol.freq + f->mod_out;
    if (!f->halt && pitch > 0) {
        f->wave_acc = (uint16_t)(f->wave_acc + pitch);
        if (f->wave_acc < pitch) f->wave_pos = (f->wave_pos + 1) & 63;
    }
}

void apu_fds_write(Apu *a, uint16_t addr, uint8_t v) {
    FdsAudio *f = &a->fds;
    if (addr < 0x4080) {
        if (f->wave_write) f->wave[addr & 63] = v & 63;
        return;
    }
    switch (addr) {
    case 0x4080: case 0x4084: {
        FdsEnvelope *e = addr == 0x4080 ? &f->vol : &f->mod;
        e->speed = v & 63;
        e->up = (v >> 6) & 1;
        e->off = v >> 7;
        fds_env_reset(f, e);  /* a write restarts the envelope's timer */
        if (e->off) e->gain = e->speed;
        fds_mod_update(f);
        break;
    }
    case 0x4082: f->vol.freq = (uint16_t)((f->vol.freq & 0xF00) | v); fds_mod_update(f); break;
    case 0x4083:
        f->env_halt = (v >> 6) & 1;
        f->halt = v >> 7;
        if (f->halt) f->wave_pos = 0;
        if (f->env_halt) fds_env_reset(f, &f->vol), fds_env_reset(f, &f->mod);
        f->vol.freq = (uint16_t)((f->vol.freq & 0xFF) | (v & 15) << 8);
        fds_mod_update(f);
        break;
    case 0x4085: fds_set_counter(f, v & 0x7F); fds_mod_update(f); break;
    case 0x4086: f->mod.freq = (uint16_t)((f->mod.freq & 0xF00) | v); break;
    case 0x4087:
        f->mod.freq = (uint16_t)((f->mod.freq & 0xFF) | (v & 15) << 8);
        f->mod_off = v >> 7;
        if (f->mod_off) f->mod_acc = 0;
        break;
    case 0x4088:  /* table writes only while the modulator is off; each fills two steps */
        if (f->mod_off) {
            f->mod_table[f->mod_pos] = f->mod_table[(f->mod_pos + 1) & 63] = v & 7;
            f->mod_pos = (f->mod_pos + 2) & 63;
        }
        break;
    case 0x4089: f->master = v & 3; f->wave_write = v >> 7; break;
    case 0x408A: f->env_speed = v; break;
    default: break;
    }
}

uint8_t apu_fds_read(const Apu *a, uint16_t addr, uint8_t open_bus) {
    const FdsAudio *f = &a->fds;
    if (addr <= 0x407F) return (uint8_t)((open_bus & 0xC0) | f->wave[f->wave_write ? addr & 63 : f->wave_pos]);
    if (addr == 0x4090) return (uint8_t)((open_bus & 0xC0) | f->vol.gain);
    if (addr == 0x4092) return (uint8_t)((open_bus & 0xC0) | f->mod.gain);
    return open_bus;
}

/* ---- VRC7 (nesdev "VRC7 audio" for the registers, patches and
 * frequency; the operator internals follow the YM2413/OPL structure as
 * emu2413 models it: the wiki does not give them) ---- */
static const uint8_t VRC7_PATCHES[15][8] = {  /* instruments 1-15, bytes $00-$07 (wiki dump) */
    {0x03, 0x21, 0x05, 0x06, 0xE8, 0x81, 0x42, 0x27}, {0x13, 0x41, 0x14, 0x0D, 0xD8, 0xF6, 0x23, 0x12},
    {0x11, 0x11, 0x08, 0x08, 0xFA, 0xB2, 0x20, 0x12}, {0x31, 0x61, 0x0C, 0x07, 0xA8, 0x64, 0x61, 0x27},
    {0x32, 0x21, 0x1E, 0x06, 0xE1, 0x76, 0x01, 0x28}, {0x02, 0x01, 0x06, 0x00, 0xA3, 0xE2, 0xF4, 0xF4},
    {0x21, 0x61, 0x1D, 0x07, 0x82, 0x81, 0x11, 0x07}, {0x23, 0x21, 0x22, 0x17, 0xA2, 0x72, 0x01, 0x17},
    {0x35, 0x11, 0x25, 0x00, 0x40, 0x73, 0x72, 0x01}, {0xB5, 0x01, 0x0F, 0x0F, 0xA8, 0xA5, 0x51, 0x02},
    {0x17, 0xC1, 0x24, 0x07, 0xF8, 0xF8, 0x22, 0x12}, {0x71, 0x23, 0x11, 0x06, 0x65, 0x74, 0x18, 0x16},
    {0x01, 0x02, 0xD3, 0x05, 0xC9, 0x95, 0x03, 0x02}, {0x61, 0x63, 0x0C, 0x00, 0x94, 0xC0, 0x33, 0xF6},
    {0x21, 0x72, 0x0D, 0x00, 0xC1, 0xD5, 0x56, 0x06}};
static const uint8_t VRC7_MUL2[16] = {1, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 20, 24, 24, 30, 30};  /* 2 x multiplier */
/* key level scaling at octave 7, by F-number bits 8-5, in 0.375 dB (OPL table: 0-42 dB) */
static const uint8_t VRC7_KSL[16] = {0, 48, 64, 74, 80, 86, 90, 94, 96, 100, 102, 104, 106, 108, 110, 112};
enum { EG_ATTACK, EG_DECAY, EG_SUSTAIN, EG_RELEASE };

/* One operator's output for wave position idx (10 bits, 1024 = 2 pi) at
 * attenuation att (0.375 dB units): sine via the log-sin and exp tables
 * (+-4084); half: negative half-wave silent. */
static int vrc7_wave(int idx, int att, int half) {
    idx &= 1023;
    int neg = idx & 512;
    if (neg && half) return 0;
    int q = idx & 255;
    if (idx & 256) q = 255 - q;
    int l = LOGSIN[q] + (att << 4);
    if (l >= 13 * 256) return 0;
    int v = ((EXP[(l & 255) ^ 255] | 0x400) << 1) >> (l >> 8);
    return neg ? -v : v;
}

/* Envelope: rate R (1-15, 0 halts) plus the key scale (block and F-number
 * bit 8; >> 2 without KSR) makes r = 4R + ks; the counter adds (4 + r % 4)
 * << r / 4 each sample and steps once per 2^15. Attack steps env down by
 * env / 8 + 1 (instant from r = 60), the rest step it up. After decay to
 * the sustain level: hold (EG type 1) or keep going at the release rate. */
static void vrc7_envelope(Vrc7Op *o, const uint8_t *p, int op, int ks, int sus) {
    int egtype = p[op] >> 5 & 1, rate;
    switch (o->state) {
    case EG_ATTACK: rate = p[4 + op] >> 4; break;
    case EG_DECAY: rate = p[4 + op] & 15; break;
    case EG_SUSTAIN: rate = egtype ? 0 : p[6 + op] & 15; break;
    default: rate = sus ? 5 : egtype ? p[6 + op] & 15 : 7; break;  /* key off: $2X bit 5 = 5; as emu2413 */
    }
    if (!rate) return;
    int r = 4 * rate + ((p[op] & 0x10) ? ks : ks >> 2);
    if (r > 63) r = 63;
    o->eg_acc += (uint32_t)(4 + (r & 3)) << (r >> 2);
    int steps = (int)(o->eg_acc >> 15);
    o->eg_acc &= 0x7FFF;
    if (o->state == EG_ATTACK) {
        if (r >= 60) o->env = 0;
        while (steps-- > 0 && o->env) o->env = (uint8_t)(o->env - (o->env >> 3) - 1);
        if (o->env == 0) o->state = EG_DECAY;
        return;
    }
    int env = o->env + steps;
    o->env = (uint8_t)(env > 127 ? 127 : env);
    if (o->state == EG_DECAY && o->env >= (p[6 + op] >> 4) * 8) o->state = EG_SUSTAIN;
}

static void vrc7_sample(Apu *a) {
    Vrc7Audio *s = &a->vrc7;
    s->am_pos = (s->am_pos + 1) % 13440;  /* tremolo: 3.7 Hz triangle, 0-13 (4.8 dB) */
    s->pm_pos = (s->pm_pos + 1) % 7776;   /* vibrato: 6.4 Hz triangle, -8..8 / 1024 (about 13 cents) */
    int am = (int)(s->am_pos < 6720 ? s->am_pos : 13440 - s->am_pos) * 13 / 6720;
    int t = (int)(s->pm_pos * 32 / 7776), pm = t < 8 ? t : t < 24 ? 16 - t : t - 32;
    int32_t sum = 0;
    for (int ch = 0; ch < 6; ch++) {
        int inst = s->reg[0x30 + ch] >> 4;
        const uint8_t *p = inst ? VRC7_PATCHES[inst - 1] : s->reg;
        int fnum = s->reg[0x10 + ch] | (s->reg[0x20 + ch] & 1) << 8, block = (s->reg[0x20 + ch] >> 1) & 7;
        int ks = (block << 1) | (fnum >> 8), sus = (s->reg[0x20 + ch] >> 5) & 1;
        int ksl = VRC7_KSL[fnum >> 5] - (7 - block) * 16;
        if (ksl < 0) ksl = 0;
        int mod = 0;
        for (int op = 0; op < 2; op++) {
            Vrc7Op *o = &s->op[ch][op];
            vrc7_envelope(o, p, op, ks, sus);
            int k = p[2 + op] >> 6;
            int att = o->env + (k ? ksl >> (3 - k) : 0) + ((p[op] & 0x80) ? am : 0) +
                      (op ? (s->reg[0x30 + ch] & 15) * 8 : (p[2] & 63) * 2);
            if (att > 127) att = 127;
            int idx = (int)(o->phase >> 9);
            if (op == 0) {  /* feedback: the last two outputs, 4 pi at FB 7 */
                int fb = p[3] & 7;
                if (fb) idx += (o->out[0] + o->out[1]) >> (9 - fb);
            } else {
                idx += mod;  /* the modulator's +-4084 is +-8 pi */
            }
            int v = vrc7_wave(idx, att, op ? (p[3] >> 4) & 1 : (p[3] >> 3) & 1);
            o->out[1] = o->out[0];
            o->out[0] = (int16_t)v;
            if (op == 0) mod = v;
            else sum += v;
            uint32_t inc = ((uint32_t)(fnum * VRC7_MUL2[p[op] & 15]) << block) >> 1;
            if (p[op] & 0x40) inc = (uint32_t)((int32_t)inc + ((int32_t)inc * pm >> 10));
            o->phase = (o->phase + inc) & 0x7FFFF;
        }
    }
    if (sum != s->out) s->out = sum, a->mix_dirty = 1;
}

static void vrc7_clock(Apu *a) {
    Vrc7Audio *s = &a->vrc7;
    if (s->tick) { s->tick--; return; }
    s->tick = 35;
    if (!s->silent) vrc7_sample(a);
}

/* How many of the next cycles (up to target) change nothing but timer
 * counts, the frame counter's count and the output sum: no timer reaching
 * its event, no frame counter step, no output sample, mixer up to date.
 * 0 with expansion sound (its own per-cycle clocks). */
static int64_t quiet_cycles(const Apu *a, uint64_t target) {
    if (a->mix_dirty || a->fc_delay || a->has_mmc5 || a->has_5b || a->has_vrc6 || a->has_vrc7 || a->has_fds ||
        (a->cart && a->cart->n163_sound))
        return 0;
    /* e: the first of the next cycles (1 = the next one) with an event */
    int64_t e = (int64_t)(target - a->cycle) + 1;
    int64_t first_odd = (a->cycle + 1) & 1 ? 1 : 2;  /* pulses tick on odd cycles */
    for (int i = 0; i < 2; i++) {
        int64_t p = first_odd + 2 * (int64_t)a->pulse[i].timer;  /* the tick that finds 0 */
        if (p < e) e = p;
    }
    if (a->tri.timer + 1 < e) e = a->tri.timer + 1;
    if (a->noise.timer < e) e = a->noise.timer ? a->noise.timer : 1;  /* --timer == 0 */
    if (a->dmc.timer < e) e = a->dmc.timer ? a->dmc.timer : 1;
    const int32_t *s = FC_STEP[a->pal];
    int32_t steps[6] = {s[0], s[1], s[2], a->fc_mode ? s[4] : s[3], a->fc_mode ? 1 : s[3] + 1, s[3] + 2};
    for (int i = 0; i < 6; i++)
        if (steps[i] > a->fc_count && steps[i] - a->fc_count < e) e = steps[i] - a->fc_count;
    if (a->fc_count < 0) e = 1;  /* not expected; let the slow path handle it */
    int64_t sample = (a->cpu_hz - a->frac + APU_SAMPLE_RATE - 1) / APU_SAMPLE_RATE;  /* frac crosses cpu_hz */
    if (sample < e) e = sample;
    return e - 1;
}

void apu_run(Apu *a, uint64_t target) {
    while (a->cycle < target) {
        int64_t k = quiet_cycles(a, target);
        if (k > 0) {  /* what k one-cycle steps below would do */
            int ticks = (int)((a->cycle + k + 1) / 2 - (a->cycle + 1) / 2);  /* odd cycles in (cycle, cycle + k] */
            a->pulse[0].timer = (uint16_t)(a->pulse[0].timer - ticks);
            a->pulse[1].timer = (uint16_t)(a->pulse[1].timer - ticks);
            a->tri.timer = (uint16_t)(a->tri.timer - k);
            a->noise.timer = (uint16_t)(a->noise.timer - k);
            a->dmc.timer = (uint16_t)(a->dmc.timer - k);
            a->fc_count += (int32_t)k;
            for (int64_t i = 0; i < k; i++) a->acc += a->mix_last;  /* one add per cycle: the same sum */
            a->acc_n += (int)k;
            a->frac += APU_SAMPLE_RATE * k;
            a->cycle += (uint64_t)k;
            continue;
        }
        a->cycle++;
        frame_counter(a);
        timers(a);
        if (a->cart && a->cart->n163_sound && cart_n163_clock(a->cart) && a->n163_gain != 0) a->mix_dirty = 1;
        if (a->has_mmc5) mmc5_clock(a);
        if (a->has_5b) s5b_clock(a);
        if (a->has_vrc6) vrc6_clock(a);
        if (a->has_vrc7) vrc7_clock(a);
        if (a->has_fds) fds_clock(a);
        output(a);
    }
}

int64_t apu_cycles_to_event(const Apu *a) {
    if (a->dmc.dma_request) return 0;
    int64_t best = INT32_MAX;
    if (a->fc_delay) best = a->fc_delay;
    else if (a->fc_mode == 0 && !a->fc_inhibit) {
        int32_t c = a->fc_count;
        /* the CPU sees the flag a cycle after the APU sets it; at 29830-29832
         * (NTSC) it is (re)set now, so visible next cycle (seen after OAM DMA:
         * cpu_interrupts_v2 4-irq_and_dma) */
        int32_t irq = FC_STEP[a->pal][3];
        best = c < irq ? irq - c + 1 : 1;
    }
    const Dmc *d = &a->dmc;
    if (d->remaining) {  /* the buffer empties when the shift register reloads */
        int64_t t = d->timer + (int64_t)(d->bits - 1) * d->period;
        if (t < best) best = t;
    }
    return best;
}

uint8_t apu_read_status(Apu *a, uint8_t open_bus) {
    uint8_t v = (uint8_t)((open_bus & 0x20) | (a->pulse[0].len ? 1 : 0) | (a->pulse[1].len ? 2 : 0) |
                          (a->tri.len ? 4 : 0) | (a->noise.len ? 8 : 0) | (a->dmc.remaining ? 0x10 : 0) |
                          (a->frame_irq ? 0x40 : 0) | (a->dmc_irq ? 0x80 : 0));
    a->frame_irq = 0;
    return v;
}

/* A length load written on the cycle the counter is clocked is ignored when
 * the counter was non-zero before the clock (blargg 2005 "Length Reload"). */
static void load_length(Apu *a, uint8_t *len, uint8_t len_before, int channel, uint8_t v) {
    if (!(a->enabled >> channel & 1)) return;
    if (a->cycle == a->last_half_clock && len_before) return;
    *len = LENGTHS[v >> 3];
}

void apu_write(Apu *a, uint16_t addr, uint8_t v) {
    a->mix_dirty = 1;
    if (addr < 0x4008) {
        Pulse *p = &a->pulse[(addr >> 2) & 1];
        switch (addr & 3) {
        case 0:
            p->duty = v >> 6;
            p->halt = p->env.loop = (v >> 5) & 1;
            p->env.constant = (v >> 4) & 1;
            p->env.period = v & 15;
            break;
        case 1:
            p->sweep_on = v >> 7;
            p->sweep_period = (v >> 4) & 7;
            p->sweep_neg = (v >> 3) & 1;
            p->sweep_shift = v & 7;
            p->sweep_reload = 1;
            break;
        case 2: p->period = (p->period & 0x700) | v; break;
        case 3:
            p->period = (uint16_t)((p->period & 0xFF) | (v & 7) << 8);
            load_length(a, &p->len, p->len_before, (addr >> 2) & 1, v);
            p->seq = 0;
            p->env.start = 1;
            break;
        }
        return;
    }
    switch (addr) {
    case 0x4008:
        a->tri.control = v >> 7;
        a->tri.lin_value = v & 0x7F;
        break;
    case 0x400A: a->tri.period = (a->tri.period & 0x700) | v; break;
    case 0x400B:
        a->tri.period = (uint16_t)((a->tri.period & 0xFF) | (v & 7) << 8);
        load_length(a, &a->tri.len, a->tri.len_before, 2, v);
        a->tri.lin_reload = 1;
        break;
    case 0x400C:
        a->noise.halt = a->noise.env.loop = (v >> 5) & 1;
        a->noise.env.constant = (v >> 4) & 1;
        a->noise.env.period = v & 15;
        break;
    case 0x400E:
        a->noise.mode = v >> 7;
        a->noise.period = NOISE_PERIOD[a->pal][v & 15];
        break;
    case 0x400F:
        load_length(a, &a->noise.len, a->noise.len_before, 3, v);
        a->noise.env.start = 1;
        break;
    case 0x4010:
        a->dmc.irq_on = v >> 7;
        a->dmc.loop = (v >> 6) & 1;
        a->dmc.period = DMC_PERIOD[a->pal][v & 15];
        if (!a->dmc.irq_on) a->dmc_irq = 0;
        break;
    case 0x4011: a->dmc.level = v & 0x7F; break;
    case 0x4012: a->dmc.start = (uint16_t)(0xC000 | v << 6); break;
    case 0x4013: a->dmc.length = (uint16_t)(v << 4 | 1); break;
    case 0x4015:
        a->enabled = v & 0x1F;
        if (!(v & 1)) a->pulse[0].len = 0;
        if (!(v & 2)) a->pulse[1].len = 0;
        if (!(v & 4)) a->tri.len = 0;
        if (!(v & 8)) a->noise.len = 0;
        a->dmc_irq = 0;
        if (!(v & 0x10)) {
            a->dmc.remaining = 0;
        } else if (!a->dmc.remaining) {
            a->dmc.addr = a->dmc.start;
            a->dmc.remaining = a->dmc.length;
            dmc_request_if_empty(a);
        }
        break;
    case 0x4017:
        a->fc_last = v;
        a->fc_inhibit = (v >> 6) & 1;
        if (a->fc_inhibit) a->frame_irq = 0;
        a->fc_new = v >> 7;
        if ((a->cycle & 1) == FC_JITTER_PARITY) {
            a->fc_mode = a->fc_new;
            a->fc_count = 0;
            a->fc_delay = 0;
        } else {
            a->fc_delay = 1;
        }
        break;
    default: break;
    }
}

/* shortcut: PCM read mode ($5010 bit 0: the DAC takes CPU reads of
 * $8000-$BFFF) is not emulated; every ROM read would need the slow bus path,
 * and recompiled code fetches its instructions as constants. The one game
 * with PCM (Shin 4 Nin Uchi Mahjong) is checked in systems/nes/docs/verification-log.md. */
void apu_mmc5_write(Apu *a, uint16_t addr, uint8_t v) {
    Mmc5Audio *m = &a->mmc5;
    a->mix_dirty = 1;
    if (addr < 0x5008) {
        int ch = (addr >> 2) & 1;
        Pulse *p = &m->pulse[ch];
        switch (addr & 3) {
        case 0:
            p->duty = v >> 6;
            p->halt = p->env.loop = (v >> 5) & 1;
            p->env.constant = (v >> 4) & 1;
            p->env.period = v & 15;
            break;
        case 1: break;  /* no sweep unit */
        case 2: p->period = (p->period & 0x700) | v; break;
        case 3:
            p->period = (uint16_t)((p->period & 0xFF) | (v & 7) << 8);
            if (m->enabled >> ch & 1) p->len = LENGTHS[v >> 3];
            p->seq = 0;
            p->env.start = 1;
            break;
        }
    } else if (addr == 0x5010) {
        m->pcm_read_mode = v & 1;
        m->pcm_irq_on = v >> 7;
    } else if (addr == 0x5011 && !m->pcm_read_mode) {
        if (v) m->pcm = v, m->pcm_irq = 0;
        else m->pcm_irq = 1;  /* $00 leaves the DAC and trips the IRQ */
    } else if (addr == 0x5015) {
        m->enabled = v & 3;
        for (int i = 0; i < 2; i++)
            if (!(m->enabled >> i & 1)) m->pulse[i].len = 0;
    }
}

void apu_5b_write(Apu *a, uint16_t addr, uint8_t v) {
    Sunsoft5b *s = &a->s5b;
    if (addr < 0xE000) { s->addr = v; return; }
    if (s->addr & 0xF0) return;  /* writes disabled, like the YM2149F's chip select */
    s->reg[s->addr] = v;
    a->mix_dirty = 1;
    if (s->addr == 0x0D) {  /* the shape restarts the envelope */
        s->env_step = 0;
        s->env_hold = 0;
        s->env_count = 0;
        s->env_attack = (v >> 2) & 1;
        s->env_level = s->env_attack ? 0 : 31;
    }
}

void apu_vrc6_write(Apu *a, uint16_t reg, uint8_t v) {
    Vrc6Audio *s = &a->vrc6;
    a->mix_dirty = 1;
    if (reg == 0x9003) { s->freq_ctrl = v & 7; return; }
    int ch = (reg >> 12) - 9, k = reg & 3;
    if (ch < 0 || ch > 2 || k == 3) return;
    s->reg[ch][k] = v;
    if (k == 2 && !(v & 0x80)) {  /* disabled: the duty restarts, the saw clears */
        if (ch < 2) s->step[ch] = 15;
        else s->acc = 0, s->acc_clock = 0;
    }
}

void apu_vrc7_write(Apu *a, int port, uint8_t v) {
    Vrc7Audio *s = &a->vrc7;
    if (s->silent) return;
    if (port == 0) { s->addr = v; return; }
    uint8_t r = s->addr;
    if (!(r < 0x08 || (r >= 0x10 && r < 0x16) || (r >= 0x20 && r < 0x26) || (r >= 0x30 && r < 0x36))) return;
    if (r >= 0x20 && r < 0x26 && ((s->reg[r] ^ v) & 0x10)) {  /* key on: restart; key off: release */
        Vrc7Op *o = s->op[r - 0x20];
        int inst = s->reg[0x30 + r - 0x20] >> 4;
        const uint8_t *p = inst ? VRC7_PATCHES[inst - 1] : s->reg;
        for (int op = 0; op < 2; op++) {
            if (v & 0x10) o[op].state = EG_ATTACK, o[op].phase = 0, o[op].eg_acc = 0, o[op].out[0] = o[op].out[1] = 0;
            else if (op || !(p[0] & 0x20)) o[op].state = EG_RELEASE;  /* a sustained modulator keeps sustaining */
        }
    }
    s->reg[r] = v;
}

void apu_vrc7_silence(Apu *a, int on) {
    Vrc7Audio *s = &a->vrc7;
    if (on && !s->silent) {  /* registers cleared, tremolo reset (vibrato kept) */
        memset(s->reg, 0, sizeof s->reg);
        memset(s->op, 0, sizeof s->op);
        for (int i = 0; i < 6; i++) s->op[i][0].env = s->op[i][1].env = 127, s->op[i][0].state = s->op[i][1].state = EG_RELEASE;
        s->am_pos = 0;
        s->out = 0;
        a->mix_dirty = 1;
    }
    s->silent = (uint8_t)on;
}

uint8_t apu_mmc5_read(Apu *a, uint16_t addr, uint8_t open_bus) {
    Mmc5Audio *m = &a->mmc5;
    if (addr == 0x5015) return (uint8_t)((open_bus & 0xFC) | (m->pulse[0].len ? 1 : 0) | (m->pulse[1].len ? 2 : 0));
    if (addr == 0x5010) {  /* bit 7: IRQ (reading acknowledges); bit 0: 0 = letterless MMC5 */
        uint8_t r = (uint8_t)((open_bus & 0x7E) | (m->pcm_irq && m->pcm_irq_on) << 7);
        m->pcm_irq = 0;
        return r;
    }
    return open_bus;
}

void apu_dmc_fill(Apu *a, uint8_t byte) {
    Dmc *d = &a->dmc;
    d->dma_request = 0;
    if (!d->remaining) return;  /* channel disabled meanwhile */
    d->buffer = byte;
    d->buffer_full = 1;
    d->addr = d->addr == 0xFFFF ? 0x8000 : (uint16_t)(d->addr + 1);
    if (--d->remaining == 0) {
        if (d->loop) {
            d->addr = d->start;
            d->remaining = d->length;
        } else if (d->irq_on) {
            a->dmc_irq = 1;
        }
    }
}

int apu_take_samples(Apu *a, float *out, int max) {
    int n = a->buf_len < max ? a->buf_len : max;
    memcpy(out, a->buf, (size_t)n * sizeof *out);
    memmove(a->buf, a->buf + n, (size_t)(a->buf_len - n) * sizeof *out);
    a->buf_len -= n;
    return n;
}
