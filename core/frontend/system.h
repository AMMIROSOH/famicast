/* What a console gives the SDL3 frontend (sdl3_main.c). Each program links
 * one system (the NES: nes/frontend.c); the frontend owns the window, sound,
 * input, pacing, settings file and state keys, the system everything else.
 * The frontend calls sys_frame, sys_reset, sys_key and the state functions
 * from its emulator thread or under its lock, never at the same time. */
#ifndef CORE_SYSTEM_H
#define CORE_SYSTEM_H

#include <stddef.h>
#include <stdint.h>

#include "input_log.h"
#include "settings.h"

typedef struct {
    int width, height;        /* frame buffer, XRGB8888 */
    int cols, lines;          /* the visible part, centred in it */
    double fps;               /* frames per second the game runs at */
    double aspect;            /* shown pixel width / height (1 = square) */
    int sample_rate;          /* mono float samples */
    char name[256];           /* the game, for the window title */
    char tag[16];             /* after it: "Game (NTSC)" */
    char state_path[1024];    /* F2 / F4 */
} SysInfo;

/* The system's own settings: offered every key the frontend does not
 * know (1 = taken), and appended to the default settings file. */
extern const char SYS_SETTINGS[];
int sys_setting(const char *key, const char *value);

/* Load the game: `path` from the command line, or NULL for the one the
 * program embeds. Called after the settings are loaded. 0 on success, else
 * a message in err. */
int sys_open(const char *path, const Settings *cfg, SysInfo *info, char *err, size_t errlen);
void sys_reset(void);
/* One frame with this input: the picture into rgb (width * height), the
 * sound into samples (at most max). Returns the number of samples. */
int sys_frame(const InputFrame *in, uint32_t *rgb, float *samples, int max);
/* Why the game stopped (a crashed CPU), as lines for the picture, or NULL. */
const char *sys_halted(char *buf, size_t len);
int sys_state_save(const char *path);
int sys_state_load(const char *path, char *err, size_t errlen);
/* A key the system handles itself (an SDL keycode): a message for the
 * title bar, or NULL. */
const char *sys_key(int key, char *buf, size_t len);
/* Quitting: write saves and whatever else the game keeps. */
void sys_close(void);

#endif
