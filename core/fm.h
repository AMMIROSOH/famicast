/* The FM operator of Yamaha's OPN / OPM chips (the Genesis' YM2612, the
 * CPS-1's YM2151): a phase through a log-sin table and exp, an envelope
 * of attack, decay, sustain and release, and the 8 ways (algorithms) four
 * operators connect. Each chip keeps its own channels, frequencies, LFO
 * and timers. */
#ifndef CORE_FM_H
#define CORE_FM_H

#include <stdint.h>

enum { FM_ATTACK, FM_DECAY, FM_SUSTAIN, FM_RELEASE };

typedef struct {
    uint8_t dt, mul, tl, ks, ar, am, dr, sr, sl, rr;
    uint8_t key, state;       /* state: FM_ATTACK ... FM_RELEASE */
    uint16_t att;             /* envelope attenuation, 10 bits (0.09375 dB) */
    uint32_t phase;           /* 20 bits */
} FmOp;

void fm_tables(void);                             /* before the rest; once is enough */
void fm_key(FmOp *o, int on);
void fm_eg_step(FmOp *o, int kc, uint32_t eg_cnt);  /* kc: the 5-bit key code */
int fm_op_out(const FmOp *o, int mod, int am);    /* phase plus modulation, through log-sin and exp: +-8168 */
int fm_detune(int dt, int kc);                    /* DT1 in phase increments, negative for dt bit 2 */
/* S1-S4 through algorithm alg, S1 fed back by fb (0-7, fb_out its last two
 * outputs, updated); am: each operator's AM. The channel's output, -8192..8191. */
int fm_algorithm(FmOp op[4], int alg, int fb, int16_t fb_out[2], const int am[4]);

#endif
