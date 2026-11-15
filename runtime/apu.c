#include "apu.h"

#include <string.h>

static const uint8_t LENGTHS[32] = {10, 254, 20, 2,  40, 4,  80, 6,  160, 8,  60, 10, 14, 12, 26, 14,
                                    12, 16,  24, 18, 48, 20, 96, 22, 192, 24, 72, 26, 16, 28, 32, 30};
static const uint8_t DUTY[4][8] = {{0, 1, 0, 0, 0, 0, 0, 0}, {0, 1, 1, 0, 0, 0, 0, 0},
                                   {0, 1, 1, 1, 1, 0, 0, 0}, {1, 0, 0, 1, 1, 1, 1, 1}};
static const uint8_t TRIANGLE[32] = {15, 14, 13, 12, 11, 10, 9,  8,  7,  6,  5,  4,  3,  2,  1,  0,
                                     0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15};
/* NTSC periods in CPU cycles (nesdev "APU Noise", "APU DMC") */
static const uint16_t NOISE_PERIOD[16] = {4, 8, 16, 32, 64, 96, 128, 160, 202, 254, 380, 508, 762, 1016, 2034, 4068};
static const uint16_t DMC_PERIOD[16] = {428, 380, 340, 320, 286, 254, 226, 214, 190, 160, 142, 128, 106, 84, 72, 54};

enum { CPU_HZ = 1789773 };

static void mixer_tables(void);
static float PULSE_OUT[31], TND_OUT[203];  /* mixer tables, see mix() */
/* $4017 writes take effect on even APU clocks: one cycle later when written
 * on the other parity (blargg 2005 "Clock Jitter"; parity chosen by test). */
enum { FC_JITTER_PARITY = 1 };

static void quarter_frame(Apu *a);
static void half_frame(Apu *a);

void apu_reset(Apu *a, Cart *cart, int power_on) {
    if (power_on) {
        if (TND_OUT[1] == 0) mixer_tables();
        memset(a, 0, sizeof *a);
        a->noise.lfsr = 1;
        a->noise.period = a->noise.timer = NOISE_PERIOD[0];
        a->pulse[0].ones_complement = 1;
        a->dmc.period = DMC_PERIOD[0];
        a->dmc.timer = DMC_PERIOD[0];
        a->dmc.bits = 8;
        a->dmc.silence = 1;
    }
    a->cart = cart;
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
    if (a->fc_mode == 0) {
        switch (c) {
        case 7459: quarter_frame(a); break;
        case 14915: quarter_frame(a); half_frame(a); break;
        case 22373: quarter_frame(a); break;
        case 29830: irq = 1; break;
        case 29831: quarter_frame(a); half_frame(a); irq = 1; break;
        case 29832: irq = 1; a->fc_count = 2; break;
        }
    } else {
        switch (c) {
        case 1: quarter_frame(a); half_frame(a); break;
        case 7459: quarter_frame(a); break;
        case 14915: quarter_frame(a); half_frame(a); break;
        case 22373: quarter_frame(a); break;
        case 37283: quarter_frame(a); half_frame(a); a->fc_count = 1; break;
        }
    }
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

static void timers(Apu *a) {
    if (a->cycle & 1) {  /* pulse timers run on APU cycles (every other CPU cycle) */
        for (int i = 0; i < 2; i++) {
            Pulse *p = &a->pulse[i];
            if (p->timer == 0) { p->timer = p->period; p->seq = (p->seq + 1) & 7; a->mix_dirty = 1; }
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
        a->mix_dirty = 1;
    }
    Dmc *d = &a->dmc;
    if (--d->timer == 0) {
        d->timer = d->period;
        dmc_clock(a);
        a->mix_dirty = 1;
    }
}

/* ---- mixer (nesdev "APU Mixer", lookup-table form) ----
 * Tables built once from the same float formulas, so the output is the same
 * bit for bit; mix() runs every CPU cycle. */

static void mixer_tables(void) {
    for (int p = 1; p < 31; p++) PULSE_OUT[p] = 95.52f / (8128.0f / (float)p + 100.0f);
    for (int t = 1; t < 203; t++) TND_OUT[t] = 163.67f / (24329.0f / (float)t + 100.0f);
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
    return PULSE_OUT[p] + TND_OUT[3 * t + 2 * nz + a->dmc.level];
}

/* Box-filter to 48 kHz, then the NES's own output filters: high-pass 90 Hz
 * and 440 Hz, low-pass 14 kHz (nesdev "APU Mixer"). */
static void output(Apu *a) {
    if (a->mix_dirty) a->mix_last = mix(a), a->mix_dirty = 0;
    a->acc += a->mix_last;
    a->acc_n++;
    a->frac += APU_SAMPLE_RATE;
    if (a->frac < CPU_HZ) return;
    a->frac -= CPU_HZ;
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

void apu_run(Apu *a, uint64_t target) {
    while (a->cycle < target) {
        a->cycle++;
        frame_counter(a);
        timers(a);
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
         * it is (re)set now, so visible next cycle (seen after OAM DMA:
         * cpu_interrupts_v2 4-irq_and_dma) */
        best = c < 29830 ? 29830 - c + 1 : 1;
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
        a->noise.period = NOISE_PERIOD[v & 15];
        break;
    case 0x400F:
        load_length(a, &a->noise.len, a->noise.len_before, 3, v);
        a->noise.env.start = 1;
        break;
    case 0x4010:
        a->dmc.irq_on = v >> 7;
        a->dmc.loop = (v >> 6) & 1;
        a->dmc.period = DMC_PERIOD[v & 15];
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
