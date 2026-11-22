#ifndef ATARI2600_TIA_H
#define ATARI2600_TIA_H

#include <stdint.h>

enum { A26_WIDTH = 160, A26_HEIGHT = 312 };

typedef struct {
    uint8_t reg[0x2D];
    /* Keep the original state layout by using the unused register slots. */
    uint8_t pf_pixel, pf_reflect, rsync_skip, next_hmove;
    uint8_t motion_counter, moving, audio_clock, reserved[12];
    uint8_t collision[8];
    uint8_t input[6];
    uint8_t fire[2];
    uint8_t delayed_player[2], delayed_ball;
    uint8_t pending_value[64], pending_delay[64];
    unsigned pending_count;
    uint8_t frame[A26_WIDTH * A26_HEIGHT];
    unsigned x, y, frames;
    int position[5];
    int waiting;
    int previous_vsync;
    int hmove_blank;
    unsigned color_rate, sample_phase;
    uint64_t video_hash, audio_hash;
    uint8_t audio_divider[2], audio_pulse[2], audio_noise[2];
    uint8_t audio_enable[2], audio_feedback[2], audio_noise_bit[2], audio_hold[2];
    float samples[2048];
    unsigned sample_count;
} A26Tia;

void a26_tia_init(A26Tia *tia);
void a26_tia_tick(void *ctx);
uint8_t a26_tia_read(void *ctx, uint16_t address, uint8_t open_bus);
void a26_tia_write(void *ctx, uint16_t address, uint8_t value);
int a26_tia_ready(void *ctx);
int a26_tia_samples(A26Tia *tia, float *samples, int max);

#endif
