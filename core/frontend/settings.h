/* Frontend settings: an INI-style file next to the program
 * (<program>.ini; in retrocast <system>.ini), created with defaults and
 * comments when missing. Keys the frontend does not know go to the system
 * (system.h: System.setting). */
#ifndef CORE_SETTINGS_H
#define CORE_SETTINGS_H

enum { SCALE_FIT, SCALE_INTEGER, SCALE_STRETCH };

typedef struct {
    int fullscreen;    /* 0/1 */
    int scale;         /* SCALE_* */
    int tv_frame;      /* 0/1 */
    int window_scale;  /* starting window size, multiples of the content size */
    int record_misses; /* recompiled games: keep interpreted ROM code in <program>.misses */
    int aspect_tv;     /* 1: pixels as on a TV (the system's aspect); 0: square */
} Settings;

/* Fills s with defaults, then the values found in path. Writes a commented
 * default file if path does not exist, with the system's own settings
 * (sys_text) in it. Bad lines are reported on stderr. Every other key is
 * passed to sys_setting (1 = taken). */
void settings_load(Settings *s, const char *path, const char *sys_text,
                   int (*sys_setting)(const char *key, const char *value));

/* "1"/"on"/"yes"/"true" or "0"/"off"/"no"/"false" into *out; 0 if neither. */
int settings_bool(const char *value, int *out);

#endif
