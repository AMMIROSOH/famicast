/* 2A03 APU: pulse x2, triangle, noise, DMC, frame counter, mixer.
 * References: nesdev wiki "APU", "APU Frame Counter", "APU Length Counter",
 * "APU Envelope", "APU Sweep", "APU Triangle", "APU Noise", "APU DMC",
 * "APU Mixer"; blargg's 2005 frame counter notes (blargg_apu_2005.07.30
 * readme). Run lazily by the bus: apu_run() catches up to a CPU cycle. */
#ifndef NES_APU_H
#define NES_APU_H

#include <stdint.h>

#include "cart.h"

typedef struct {
    uint8_t start, divider, decay, period, loop, constant;
} Envelope;

typedef struct {
    uint8_t duty, seq, halt, len, len_before;
    Envelope env;
    uint16_t period, timer;
    uint8_t sweep_on, sweep_period, sweep_neg, sweep_shift, sweep_div, sweep_reload;
    uint8_t ones_complement;  /* pulse 1 negates with -c-1 */
} Pulse;

typedef struct {
    uint8_t control, lin_value, lin, lin_reload, len, len_before, seq;
    uint16_t period, timer;
} Triangle;

typedef struct {
    uint8_t halt, mode, len, len_before;
    Envelope env;
    uint16_t period, timer, lfsr;
} Noise;

typedef struct {
    uint8_t irq_on, loop, level, buffer, buffer_full, shift, bits, silence;
    uint16_t period, timer, start, length, addr, remaining;
    uint8_t dma_request;  /* buffer empty with bytes left: bus must fetch */
} Dmc;

/* MMC5 sound (nesdev "MMC5 audio"): two pulses without sweep, envelope and
 * length clocked together every 7,424 CPU cycles (~240 Hz), an 8-bit PCM DAC. */
typedef struct {
    Pulse pulse[2];
    uint8_t enabled;                  /* $5015 */
    uint8_t pcm, pcm_read_mode, pcm_irq_on, pcm_irq;  /* $5011; $5010 bits 0, 7; IRQ tripped by $00 */
    uint16_t frame;                   /* CPU cycles since the last envelope/length clock */
} Mmc5Audio;

/* Sunsoft 5B sound (nesdev "Sunsoft 5B audio"): a YM2149F with SEL low.
 * Three square channels, noise and an envelope, all stepped by one tick
 * every 16 CPU cycles. */
typedef struct {
    uint8_t addr, reg[16];            /* $C000 select (high nibble set: $E000 writes ignored), registers */
    uint8_t tick;                     /* CPU cycles to the next 16-cycle tick */
    uint16_t tone_count[3];
    uint8_t tone_out[3];
    uint8_t noise_count, noise_half;  /* a new noise bit every second period */
    uint32_t lfsr;                    /* 17 bits */
    uint16_t env_count;
    uint8_t env_step, env_attack, env_hold, env_level;  /* level 0-31 */
} Sunsoft5b;

/* Konami VRC6 sound (nesdev "VRC6 audio"): two pulses with 16-step duty and
 * a sawtooth, each on a 12-bit divider clocked every CPU cycle. */
typedef struct {
    uint8_t reg[3][3];                /* $9000-$9002, $A000-$A002, $B000-$B002 */
    uint8_t freq_ctrl;                /* $9003: bit 0 halt, 1 periods >> 4, 2 periods >> 8 */
    uint16_t timer[3];
    uint8_t step[2];                  /* pulse duty steps, 15 down to 0 */
    uint8_t acc, acc_clock;           /* saw accumulator; divider clocks in this ramp (0-13) */
} Vrc6Audio;

/* Konami VRC7 sound (nesdev "VRC7 audio"): a YM2413 (OPLL) with six
 * two-operator FM channels, 15 built-in instruments and one custom, one
 * sample every 36 CPU cycles (49,716 Hz). */
typedef struct {
    uint32_t phase;                   /* 19 bits; the top 10 index the wave (1024 = 2 pi) */
    uint32_t eg_acc;                  /* envelope rate accumulator */
    uint8_t state, env;               /* envelope phase; level 0-127 in 0.375 dB steps */
    int16_t out[2];                   /* the last two outputs (modulator feedback) */
} Vrc7Op;

typedef struct {
    uint8_t addr, reg[0x40];          /* $9010 select; registers $00-$07, $10-$15, $20-$25, $30-$35 */
    uint8_t silent;                   /* $E000 bit 6: silenced and cleared, writes ignored */
    uint8_t tick;                     /* CPU cycles to the next sample */
    uint32_t am_pos, pm_pos;          /* tremolo and vibrato LFO positions, in samples */
    Vrc7Op op[6][2];                  /* [channel][0: modulator, 1: carrier] */
    int32_t out;                      /* the six carriers, summed */
} Vrc7Audio;

enum { APU_SAMPLE_RATE = 48000, APU_BUF = 8192 };

typedef struct Apu {
    Cart *cart;
    Pulse pulse[2];
    Triangle tri;
    Noise noise;
    Dmc dmc;
    uint8_t enabled;          /* $4015 channel enable bits */
    uint64_t cycle;           /* last CPU cycle processed */
    /* frame counter: counts CPU cycles from the (parity-adjusted) $4017 write */
    uint8_t fc_mode, fc_inhibit;
    int32_t fc_count;
    int32_t fc_delay;         /* >0: $4017 write pending, cycles until it applies */
    uint8_t fc_new, fc_last;  /* pending mode; last value written (reset rewrites it) */
    uint64_t last_half_clock; /* cycle of the last length clock (reload rule) */
    uint8_t frame_irq, dmc_irq;
    /* audio out: box-filter resampling to 48 kHz, then the NES output filters */
    float acc;
    int acc_n;
    float n163_gain;          /* Namco 163 sound: its level -> mixer units (0: none) */
    uint8_t has_mmc5;
    Mmc5Audio mmc5;
    uint8_t has_5b;           /* mapper 69 (FME-7 boards without the 5B ignore these writes) */
    Sunsoft5b s5b;
    uint8_t has_vrc6;
    Vrc6Audio vrc6;
    uint8_t has_vrc7;         /* mapper 85 except VRC7b (submapper 1: no sound circuit) */
    Vrc7Audio vrc7;
    float mix_last;           /* mixer output, recomputed only when mix_dirty: */
    uint8_t mix_dirty;        /* set wherever channel state can change */
    int64_t frac;
    float hp1_x, hp1_y, hp2_x, hp2_y, lp_y;
    float buf[APU_BUF];
    int buf_len;
} Apu;

void apu_reset(Apu *a, Cart *cart, int power_on);
void apu_run(Apu *a, uint64_t cpu_cycle);           /* catch up to and including this cycle */
int64_t apu_cycles_to_event(const Apu *a);           /* until IRQ or DMC fetch may change */
uint8_t apu_read_status(Apu *a, uint8_t open_bus);   /* $4015 */
void apu_write(Apu *a, uint16_t addr, uint8_t v);    /* $4000-$4013, $4015, $4017 */
void apu_dmc_fill(Apu *a, uint8_t byte);             /* DMA delivered a sample byte */
static inline int apu_irq(const Apu *a) {
    return a->frame_irq || a->dmc_irq || (a->mmc5.pcm_irq && a->mmc5.pcm_irq_on);
}
/* MMC5 sound registers: $5000-$5007, $5010, $5011, $5015 (reads: $5010, $5015) */
void apu_mmc5_write(Apu *a, uint16_t addr, uint8_t v);
uint8_t apu_mmc5_read(Apu *a, uint16_t addr, uint8_t open_bus);
/* Sunsoft 5B: $C000-$DFFF register select, $E000-$FFFF data */
void apu_5b_write(Apu *a, uint16_t addr, uint8_t v);
/* VRC6: reg is the register as on mapper 24 ($9000-$9003, $A000-$A002, $B000-$B002) */
void apu_vrc6_write(Apu *a, uint16_t reg, uint8_t v);
/* VRC7: port 0 = $9010 (register select), 1 = $9030 (data); $E000 bit 6 */
void apu_vrc7_write(Apu *a, int port, uint8_t v);
void apu_vrc7_silence(Apu *a, int on);
int apu_take_samples(Apu *a, float *out, int max);   /* drain the 48 kHz output */

#endif
