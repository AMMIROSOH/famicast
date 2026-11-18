/* Input recordings, for replays and regression tests.
 * File: "NESI", version byte 1, then 3 bytes per frame:
 * pad 1 buttons, pad 2 buttons, flags (bit 0: reset before this frame).
 * Emulation is deterministic from power-on, so a recording replays exactly. */
#ifndef CORE_INPUT_LOG_H
#define CORE_INPUT_LOG_H

#include <stdint.h>
#include <stdio.h>

/* Buttons as the frontend reports them (the NES controller's shift-register
 * order; other systems map them to their own pads). */
enum {
    PAD_A = 0x01, PAD_B = 0x02, PAD_SELECT = 0x04, PAD_START = 0x08,
    PAD_UP = 0x10, PAD_DOWN = 0x20, PAD_LEFT = 0x40, PAD_RIGHT = 0x80
};

enum { INPUT_RESET = 0x01 };

typedef struct { uint8_t pad[2], flags; } InputFrame;

typedef struct {
    InputFrame *frames;
    long count;
} InputLog;

FILE *input_log_create(const char *path);  /* writes the header; NULL on error */
void input_log_write(FILE *f, InputFrame fr);
int input_log_load(const char *path, InputLog *log);  /* 0 ok, -1 error */
void input_log_free(InputLog *log);

/* Real pads cannot press left+right or up+down; keyboards and sticks can,
 * and some games misbehave. Drop both directions of such a pair. */
uint8_t input_clean(uint8_t buttons);

#endif
