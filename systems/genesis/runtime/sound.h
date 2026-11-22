/* Sound (systems/genesis/docs/roadmap.md D4): the SN76489 PSG (inside the VDP)
 * and the YM2612 (6 FM channels, channel 6 as DAC, stereo), run on the
 * master clock: one YM2612 sample every 144 of its clocks (master / 7),
 * 53,267 a second on NTSC, the PSG's counters every 16 of its clocks
 * (master / 15) averaged over each sample. Samples leave at that rate. */
#ifndef GENESIS_SOUND_H
#define GENESIS_SOUND_H

#include <stdint.h>

enum { SND_SAMPLE_CLOCKS = 144 * 7, SND_BUF = 4096 };  /* master clocks a sample; sample frames kept */

typedef struct {
    uint8_t dt, mul, tl, ks, ar, am, dr, sr, sl, rr;
    uint8_t key, state;       /* state: attack, decay, sustain, release */
    uint16_t att;             /* envelope attenuation, 10 bits (0.09375 dB) */
    uint32_t phase;           /* 20 bits */
} YmOp;

typedef struct {
    YmOp op[4];               /* S1, S2, S3, S4 (registers order them S1, S3, S2, S4) */
    uint16_t fnum;
    uint8_t block, fb, alg, ams, fms, pan;  /* pan: bit 1 left, bit 0 right */
    int16_t fb_out[2];        /* S1's last two outputs */
} YmCh;

typedef struct {
    YmCh ch[6];
    uint8_t addr[2];          /* the register each port's next data write goes to */
    uint8_t fhi, fhi3;        /* the $A4-$A6 and $AC-$AE latches */
    uint16_t fnum3[3];        /* channel 3's own F-numbers in its special mode ($A9 S1, $AA S2, $A8 S3) */
    uint8_t block3[3];
    uint8_t lfo_on, lfo_freq, lfo_step;
    uint16_t lfo_cnt;
    uint8_t mode;             /* $27: timers' load and flag enables, channel 3 mode */
    uint16_t ta, ta_cnt;      /* timer A: 10 bits, counts samples up to 1024 */
    uint8_t tb, tb_sub;       /* timer B: counts every 16 samples up to 256 */
    uint16_t tb_cnt;
    uint8_t status;           /* bit 0 timer A, bit 1 timer B overflowed */
    uint8_t dac, dac_on;
    uint8_t eg_sub;
    uint32_t eg_cnt;
} Ym;

typedef struct {
    uint16_t period[3];       /* tone, 10 bits */
    uint8_t noise;            /* bit 2 white, bits 1-0 rate (3: tone 3's) */
    uint8_t vol[4];           /* attenuation, 2 dB a step, 15 off */
    uint16_t count[4];
    uint8_t out[4];           /* the tone flip-flops; [3]: the noise's */
    uint16_t lfsr;
    uint8_t latch;            /* the register the last latch byte chose */
    uint16_t frac;            /* master clocks towards the next step */
} Psg;

typedef struct {
    Ym ym;
    Psg psg;
    uint64_t next;            /* master clock of the next sample */
    float dc_in[2], dc_out[2];  /* the output's DC block (the console's coupling capacitors) */
    int n;                    /* sample frames waiting in buf */
    float buf[2 * SND_BUF];
} Sound;

struct Md;
void snd_reset(Sound *s);     /* power on */
void ym_reset(Ym *y);         /* the YM2612's reset line (held with the Z80's) */
void snd_run(struct Md *m, uint64_t now);  /* samples up to this master clock */
uint8_t ym_read(struct Md *m, uint64_t now);  /* status */
void ym_write(struct Md *m, uint64_t now, int a, uint8_t v);  /* a: 0-3 ($4000-$4003) */
void psg_write(struct Md *m, uint64_t now, uint8_t v);
int snd_take(Sound *s, float *out, int max);  /* interleaved stereo sample frames */

#endif
