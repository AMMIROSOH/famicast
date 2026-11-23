/* The YM2151 (OPM): 8 FM channels of 4 operators (core/audio/fm.h), notes by key
 * code and fraction, the LFO, noise on channel 8, timers A and B with their
 * IRQ. One sample every 64 clocks; the CPS-1's output is mono (L + R). */
#ifndef CPS1_YM2151_H
#define CPS1_YM2151_H

#include <stdint.h>

#include "fm.h"

#define OPM_CLOCK 3579545
#define OPM_RATE (OPM_CLOCK / 64)  /* 55,930 Hz */

typedef struct {
    FmOp op[4];                    /* S1-S4: M1, C1, M2, C2 */
    uint8_t dt2[4];
    uint8_t kc, kf, fb, con, pms, ams, pan;  /* pan: bit 1 right, bit 0 left */
    int16_t fb_out[2];
} OpmCh;

typedef struct {
    OpmCh ch[8];
    uint8_t addr;
    uint8_t noise;                 /* $0F: bit 7 on, bits 0-4 frequency */
    uint8_t lfo_freq, amd, pmd, wave;
    uint8_t mode;                  /* $14 */
    uint16_t ta, ta_cnt;
    uint8_t tb;
    uint16_t tb_cnt;
    uint8_t status;                /* bit 0 timer A, bit 1 timer B */
    uint8_t irq;                   /* a flag set with its IRQ enabled */
    uint8_t test;                  /* $01: bit 1 holds the LFO in reset */
    uint32_t lfo_phase;            /* the LFO counter: bits 22-29 its position */
    uint8_t eg_sub;
    uint32_t eg_cnt;
    uint32_t noise_lfsr, noise_cnt;
    uint8_t noise_state;
} Opm;

void opm_reset(Opm *y);
void opm_write(Opm *y, int port, uint8_t v);  /* port 0: register number, 1: data */
uint8_t opm_status(const Opm *y);             /* bit 1 timer B, bit 0 timer A; never busy */
int opm_sample(Opm *y);                       /* one sample, mono (L + R) / 2: -32768..32767 */

#endif
