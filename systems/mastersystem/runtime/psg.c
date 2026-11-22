/* The SN76489 (Sega's variant in the VDP): three square channels and a noise
 * channel, written through ports $40-$7F. Counters step at the Z80 clock / 16;
 * white noise taps bits 0 and 3 of a 16-bit shift register (SMS Power,
 * Development: SN76489). */
#include <string.h>

#include "sms.h"

#define PSG_STEP_CLOCKS 16

static const float VOL[16] = {  /* 2 dB a step, the last silent */
    1.0000f, 0.7943f, 0.6310f, 0.5012f, 0.3981f, 0.3162f, 0.2512f, 0.1995f,
    0.1585f, 0.1259f, 0.1000f, 0.0794f, 0.0631f, 0.0501f, 0.0398f, 0.0f
};

static void step(SmsPsg *p) {
    for (int i = 0; i < 3; i++)
        if (--p->count[i] == 0) {
            p->count[i] = p->period[i] ? p->period[i] : 0x400;
            p->out[i] = p->period[i] > 1 ? p->out[i] ^ 1 : 1;  /* periods 0 and 1 hold the output high */
        }
    if (--p->count[3] == 0) {
        p->count[3] = (p->noise & 3) == 3 ? (p->period[2] ? p->period[2] : 0x400) : (uint16_t)(0x10 << (p->noise & 3));
        if ((p->out[3] ^= 1)) {
            unsigned fb = p->noise & 4 ? (p->lfsr ^ p->lfsr >> 3) & 1 : p->lfsr & 1;
            p->lfsr = (uint16_t)(p->lfsr >> 1 | fb << 15);
        }
    }
    float v = 0;
    for (int i = 0; i < 3; i++) v += p->out[i] ? VOL[p->vol[i]] : 0;
    v += p->lfsr & 1 ? VOL[p->vol[3]] : 0;
    p->acc += v * 0.25f;
    p->steps++;
}

void sms_psg_run(SmsPsg *p, uint64_t tstates) {
    for (; p->next <= tstates; p->next += PSG_STEP_CLOCKS) step(p);
}

void sms_psg_write(SmsPsg *p, uint8_t v) {
    if (v & 0x80) p->latch = v >> 4 & 7;
    unsigned r = p->latch, ch = r >> 1;
    if (r & 1) p->vol[ch] = v & 15;
    else if (ch == 3) p->noise = v & 7, p->lfsr = 0x8000;
    else if (v & 0x80) p->period[ch] = (uint16_t)((p->period[ch] & 0x3F0) | (v & 15));
    else p->period[ch] = (uint16_t)((p->period[ch] & 15) | (v & 0x3F) << 4);
}

void sms_psg_reset(SmsPsg *p) {
    memset(p, 0, sizeof *p);
    for (int i = 0; i < 4; i++) p->vol[i] = 15, p->count[i] = 1;
    p->lfsr = 0x8000;
    p->next = PSG_STEP_CLOCKS;
}

/* One sample, centred: the average since the last one, less the DC a
 * one-sided square wave carries (a slow high-pass, as the console's output). */
float sms_psg_take(SmsPsg *p) {
    float v = p->steps ? p->acc / (float)p->steps : 0;
    p->acc = 0;
    p->steps = 0;
    p->level += (v - p->level) * 0.002f;
    return v - p->level;
}
