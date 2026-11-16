/* Frontend display settings: an INI-style file next to the program
 * (<program>.ini), created with defaults and comments when missing. */
#ifndef NES_SETTINGS_H
#define NES_SETTINGS_H

enum { SCALE_FIT, SCALE_INTEGER, SCALE_STRETCH };

typedef struct {
    int fullscreen;    /* 0/1 */
    int scale;         /* SCALE_* */
    int tv_frame;      /* 0/1 */
    int window_scale;  /* starting window size, multiples of the content size */
    int record_misses; /* recompiled games: keep interpreted ROM code in <program>.misses */
    int overscan;      /* 1: hide the top and bottom 8 lines, as a TV did */
} Settings;

/* Fills s with defaults, then the values found in path. Writes a commented
 * default file if path does not exist. Bad lines are reported on stderr. */
void settings_load(Settings *s, const char *path);

#endif
