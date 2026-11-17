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
int apu_take_samples(Apu *a, float *out, int max);   /* drain the 48 kHz output */

#endif
