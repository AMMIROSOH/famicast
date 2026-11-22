/* Audio channel logic adapted from Stella's tia/AudioChannel.cxx,
 * Copyright (c) 1995-2026 Bradford W. Mott, Stephen Anthony and the
 * Stella Team, GPL-2.0-or-later. See docs/sources.md. */
#include "tia.h"

#include <string.h>

void a26_tia_init(A26Tia *tia) {
    memset(tia, 0, sizeof *tia);
    memset(tia->input, 0x80, sizeof tia->input);
    tia->fire[0] = tia->fire[1] = 0x80;
    tia->color_rate = 3579545;
}

static void audio_phase0(A26Tia *t, unsigned i) {
    unsigned control = t->reg[0x15 + i] & 15;
    unsigned noise = t->audio_noise[i], pulse = t->audio_pulse[i];
    if (t->audio_enable[i]) {
        t->audio_noise_bit[i] = noise & 1;
        switch (control & 3) {
        case 0: case 1: t->audio_hold[i] = 0; break;
        case 2: t->audio_hold[i] = (noise & 30) != 2; break;
        default: t->audio_hold[i] = !(noise & 1); break;
        }
        if ((control & 3) == 0)
            t->audio_feedback[i] = ((pulse ^ noise) & 1) || (!noise && pulse == 10) || !(control & 12);
        else
            t->audio_feedback[i] = (!!(noise & 4) ^ (noise & 1)) || !noise;
    }
    t->audio_enable[i] = t->audio_divider[i] == (t->reg[0x17 + i] & 31);
    if (t->audio_enable[i] || t->audio_divider[i] == 31) t->audio_divider[i] = 0;
    else t->audio_divider[i]++;
}

static void audio_phase1(A26Tia *t, unsigned i) {
    if (!t->audio_enable[i]) return;
    unsigned control = t->reg[0x15 + i] & 15;
    unsigned pulse = t->audio_pulse[i];
    int feedback;
    switch (control >> 2) {
    case 0: feedback = (!!(pulse & 2) ^ (pulse & 1)) && pulse != 10 && (control & 3); break;
    case 1: feedback = !(pulse & 8); break;
    case 2: feedback = !t->audio_noise_bit[i]; break;
    default: feedback = !((pulse & 2) || !(pulse & 14)); break;
    }
    t->audio_noise[i] = (uint8_t)((t->audio_noise[i] >> 1) | (t->audio_feedback[i] ? 16 : 0));
    if (!t->audio_hold[i]) t->audio_pulse[i] = (uint8_t)((~(pulse >> 1) & 7) | (feedback ? 8 : 0));
}

static int wrap(int x) {
    x %= 160;
    return x < 0 ? x + 160 : x;
}

static int player(const A26Tia *t, int n, int x) {
    static const unsigned copies[8] = {1, 2, 2, 3, 2, 1, 3, 1};
    static const unsigned separation[8] = {0, 16, 32, 16, 64, 0, 32, 0};
    unsigned mode = t->reg[4 + n] & 7;
    unsigned scale = mode == 5 ? 2 : mode == 7 ? 4 : 1;
    for (unsigned i = 0; i < copies[mode]; i++) {
        unsigned delta = (unsigned)wrap(x - t->position[n] - (int)(i * separation[mode]));
        if (delta >= 8 * scale) continue;
        unsigned bit = delta / scale;
        if (!(t->reg[0x0B + n] & 8)) bit = 7 - bit;
        uint8_t graphics = (t->reg[0x25 + n] & 1) ? t->delayed_player[n] : t->reg[0x1B + n];
        if (graphics & (1u << bit)) return 1;
    }
    return 0;
}

static int missile(const A26Tia *t, int n, int x) {
    static const unsigned copies[8] = {1, 2, 2, 3, 2, 1, 3, 1};
    static const unsigned separation[8] = {0, 16, 32, 16, 64, 0, 32, 0};
    if (!(t->reg[0x1D + n] & 2) || (t->reg[0x28 + n] & 2)) return 0;
    unsigned mode = t->reg[4 + n] & 7;
    unsigned width = 1u << ((t->reg[4 + n] >> 4) & 3);
    for (unsigned i = 0; i < copies[mode]; i++)
        if ((unsigned)wrap(x - t->position[2 + n] - (int)(i * separation[mode])) < width) return 1;
    return 0;
}

static int playfield(const A26Tia *t, int x) {
    unsigned bit = (unsigned)x / 4;
    if (bit >= 20) bit = t->pf_reflect ? 39 - bit : bit - 20;
    if (bit < 4) return (t->reg[0x0D] >> (bit + 4)) & 1;
    if (bit < 12) return (t->reg[0x0E] >> (11 - bit)) & 1;
    return (t->reg[0x0F] >> (bit - 12)) & 1;
}

static void apply_register(A26Tia *t, unsigned reg, uint8_t value) {
    t->reg[reg] = value;
    if (reg == 0x2B) memset(t->reg + 0x20, 0, 5);
    if (reg == 0x2A) {
        if (t->x < 68) t->hmove_blank = 1;
        t->motion_counter = 0;
        t->moving = 0x1F;
    }
    if (reg == 0x1B) t->delayed_player[1] = t->reg[0x1C];
    if (reg == 0x1C) {
        t->delayed_player[0] = t->reg[0x1B];
        t->delayed_ball = t->reg[0x1F];
    }
}

static void tick_motion(A26Tia *t) {
    if (t->x == 68 && t->hmove_blank) {
        /* The extended blank suppresses eight ordinary object clocks. */
        for (unsigned i = 0; i < 5; i++) t->position[i] = wrap(t->position[i] + 8);
    }
    if (!t->moving || (t->x & 3)) return;
    unsigned blank = t->hmove_blank ? 76 : 68;
    for (unsigned i = 0; i < 5; i++) {
        unsigned mask = 1u << i;
        if (!(t->moving & mask)) continue;
        unsigned stop = (t->reg[0x20 + i] >> 4) ^ 8;
        if (t->motion_counter == stop) t->moving &= (uint8_t)~mask;
        else if (t->x < blank) t->position[i] = wrap(t->position[i] - 1);
    }
    t->motion_counter = (t->motion_counter + 1) & 15;
}

void a26_tia_tick(void *ctx) {
    A26Tia *t = ctx;
    if (t->pending_count) {
        if (t->next_hmove) t->next_hmove--;
        for (unsigned reg = 0; reg < sizeof t->reg; reg++) {
            if (t->pending_delay[reg] && --t->pending_delay[reg] == 0) {
                apply_register(t, reg, t->pending_value[reg]);
                t->pending_count--;
                if (reg == 0x2A && t->next_hmove) {
                    t->pending_delay[reg] = t->next_hmove;
                    t->next_hmove = 0;
                    t->pending_count++;
                }
            }
        }
    }
    tick_motion(t);
    if (t->audio_clock == 9 || t->audio_clock == 81) {
        audio_phase0(t, 0); audio_phase0(t, 1);
    }
    if (t->audio_clock == 37 || t->audio_clock == 149) {
        audio_phase1(t, 0); audio_phase1(t, 1);
    }
    if (++t->audio_clock == 228) t->audio_clock = 0;
    t->sample_phase += 44100;
    if (t->sample_phase >= t->color_rate) {
        t->sample_phase -= t->color_rate;
        float mixed = 0;
        for (unsigned i = 0; i < 2; i++) {
            mixed += (t->audio_pulse[i] & 1) * (t->reg[0x19 + i] & 15) / 30.0f;
        }
        if (t->sample_count < 2048) {
            t->samples[t->sample_count++] = mixed;
            uint32_t bits;
            memcpy(&bits, &mixed, sizeof bits);
            t->audio_hash = (t->audio_hash ^ bits) * UINT64_C(1099511628211);
        }
    }
    int x = (int)t->x - t->rsync_skip - 68;
    if (x >= 0 && x < A26_WIDTH && t->y < A26_HEIGHT) {
        if (x == 0 || x == 80) t->pf_reflect = t->reg[0x0A] & 1;
        if ((x & 3) == 0) t->pf_pixel = (uint8_t)playfield(t, x);
        int pf = t->pf_pixel;
        int p0 = player(t, 0, x), p1 = player(t, 1, x);
        int m0 = missile(t, 0, x), m1 = missile(t, 1, x);
        uint8_t ball_enable = (t->reg[0x27] & 1) ? t->delayed_ball : t->reg[0x1F];
        int ball = (ball_enable & 2) && wrap(x - t->position[4]) < (1 << ((t->reg[0x0A] >> 4) & 3));
        if (!(t->reg[1] & 2)) {
            t->collision[0] |= (uint8_t)((m0 && p1 ? 0x80 : 0) | (m0 && p0 ? 0x40 : 0));
            t->collision[1] |= (uint8_t)((m1 && p0 ? 0x80 : 0) | (m1 && p1 ? 0x40 : 0));
            t->collision[2] |= (uint8_t)((p0 && pf ? 0x80 : 0) | (p0 && ball ? 0x40 : 0));
            t->collision[3] |= (uint8_t)((p1 && pf ? 0x80 : 0) | (p1 && ball ? 0x40 : 0));
            t->collision[4] |= (uint8_t)((m0 && pf ? 0x80 : 0) | (m0 && ball ? 0x40 : 0));
            t->collision[5] |= (uint8_t)((m1 && pf ? 0x80 : 0) | (m1 && ball ? 0x40 : 0));
            t->collision[6] |= (uint8_t)(ball && pf ? 0x80 : 0);
            t->collision[7] |= (uint8_t)((p0 && p1 ? 0x80 : 0) | (m0 && m1 ? 0x40 : 0));
        }
        uint8_t color = t->reg[9];
        unsigned priority = t->reg[0x0A] & 6;
        if (pf || ball) color = t->reg[8];
        if (p1 || m1) color = t->reg[7];
        if (priority == 2 && pf) color = t->reg[x < 80 ? 6 : 7];
        if (p0 || m0) color = t->reg[6];
        if ((priority & 4) && (pf || ball)) color = t->reg[8];
        unsigned location = t->y * A26_WIDTH + x;
        uint8_t pixel = (t->reg[1] & 2) || (t->hmove_blank && x < 8) ? 1 : (color & 0xFE);
        t->frame[location] = pixel;
        t->video_hash = (t->video_hash ^ ((uint64_t)location << 8) ^ pixel) * UINT64_C(1099511628211);
    }
    if (++t->x == 228) {
        if (t->rsync_skip) {
            unsigned skipped = t->rsync_skip > A26_WIDTH ? A26_WIDTH : t->rsync_skip;
            for (unsigned i = 0; i < 5; i++) t->position[i] = wrap(t->position[i] + (int)skipped);
        }
        t->x = 0;
        t->rsync_skip = 0;
        t->waiting = 0;
        t->hmove_blank = 0;
        if (++t->y == A26_HEIGHT) t->y = 0;
    }
}

uint8_t a26_tia_read(void *ctx, uint16_t address, uint8_t open_bus) {
    A26Tia *t = ctx;
    unsigned reg = address & 15;
    if (reg < 8) return (uint8_t)((open_bus & 0x3F) | t->collision[reg]);
    if (reg < 14) return (uint8_t)((open_bus & 0x7F) | t->input[reg - 8]);
    return open_bus;
}

void a26_tia_write(void *ctx, uint16_t address, uint8_t value) {
    A26Tia *t = ctx;
    unsigned reg = address & 0x3F;
    if (reg >= sizeof t->reg) return;
    if (reg == 1 && !(value & 0x40)) {
        t->input[4] = t->fire[0];
        t->input[5] = t->fire[1];
    }
    unsigned delay = 0;
    if (reg == 0x2A) delay = 6;
    else if (reg == 0x0D || reg == 0x0E || reg == 0x0F || (reg >= 0x20 && reg <= 0x24) || reg == 0x2B) delay = 2;
    else if (reg == 1 || reg == 0x0B || reg == 0x0C || (reg >= 0x1B && reg <= 0x1F)) delay = 1;
    if (delay) {
        /* An NMOS read-modify-write strobes the same address twice. */
        if (reg == 0x2A && t->pending_delay[reg]) {
            t->next_hmove = (uint8_t)delay;
            return;
        }
        t->pending_value[reg] = value;
        if (!t->pending_delay[reg]) t->pending_count++;
        t->pending_delay[reg] = (uint8_t)delay;
        return;
    }
    uint8_t previous = t->reg[reg];
    apply_register(t, reg, value);
    if (reg == 0) {
        int sync = !!(value & 2);
        if (t->previous_vsync && !sync) { t->frames++; t->y = 0; }
        t->previous_vsync = sync;
    } else if (reg == 2) t->waiting = t->x != 0;
    else if (reg == 3 && t->x < 225) {
        t->rsync_skip = (uint8_t)(225 - t->x + t->rsync_skip);
        t->x = 225;
        unsigned end = t->x - t->rsync_skip + 3;
        unsigned pixel = end > 68 ? end - 68 : 0;
        memset(t->frame + t->y * A26_WIDTH + pixel, 1, A26_WIDTH - pixel);
    } else if (reg >= 0x10 && reg <= 0x14) {
        int beam = (int)t->x - t->rsync_skip - 68;
        unsigned object = reg - 0x10;
        int latency = object < 2 ? 5 : 4;
        if (object < 2 && (t->reg[4 + object] & 5) == 5) latency++;
        if (beam < 0) t->position[object] = latency - 4;
        else if (t->hmove_blank && beam < 8)
            t->position[object] = 8 + latency - (beam < 5 ? 4 : 3);
        else t->position[object] = wrap(beam + latency);
    } else if ((reg == 0x28 || reg == 0x29) && (previous & 2) && !(value & 2)) {
        unsigned n = reg - 0x28;
        unsigned mode = t->reg[4 + n] & 7;
        int scale = mode == 5 ? 2 : mode == 7 ? 4 : 1;
        t->position[2 + n] = wrap(t->position[n] + 4 * scale);
    }
    else if (reg == 0x2C) memset(t->collision, 0, sizeof t->collision);
}

int a26_tia_ready(void *ctx) { return !((A26Tia *)ctx)->waiting; }

int a26_tia_samples(A26Tia *tia, float *samples, int max) {
    unsigned count = tia->sample_count;
    if (count > (unsigned)max) count = (unsigned)max;
    memcpy(samples, tia->samples, count * sizeof *samples);
    tia->sample_count = 0;
    return (int)count;
}
