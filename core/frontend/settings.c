#include "settings.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char HEAD[] =
    "# retrocast settings. Edit, then restart the game.\n"
    "# While playing: F11 or Alt+Enter = fullscreen, F10 = TV frame (not saved here).\n"
    "\n"
    "# fullscreen: 0 = window, 1 = fullscreen (desktop resolution)\n"
    "fullscreen = 0\n"
    "\n"
    "# scale: how the picture fills the window or screen\n"
    "#   fit     = as large as possible, aspect ratio kept\n"
    "#   integer = whole multiples only: sharpest pixels, may leave a border\n"
    "#   stretch = fill everything, aspect ratio not kept\n"
    "scale = fit\n"
    "\n"
    "# tv_frame: 1 = show the picture inside a TV-like frame\n"
    "tv_frame = 0\n"
    "\n"
    "# aspect: tv = pixels the shape the console's screen gave them (NES: 8:7,\n"
    "#         PAL 1.39:1; Game Boy: square); square = square pixels\n"
    "aspect = tv\n"
    "\n"
    "# window_scale: starting window size, in multiples of the picture (1-8)\n"
    "window_scale = 3\n"
    "\n";

static const char TAIL[] =
    "\n"
    "# record_misses: 1 = remember game code that ran in the interpreter because\n"
    "# the recompiler had not found it, in <program>.misses next to the program;\n"
    "# the next build of this game recompiles it (recompiled games only)\n"
    "record_misses = 1\n";

int settings_bool(const char *v, int *out) {
    if (!strcmp(v, "1") || !strcmp(v, "on") || !strcmp(v, "yes") || !strcmp(v, "true")) return *out = 1, 1;
    if (!strcmp(v, "0") || !strcmp(v, "off") || !strcmp(v, "no") || !strcmp(v, "false")) return *out = 0, 1;
    return 0;
}

void settings_load(Settings *s, const char *path, const char *sys_text,
                   int (*sys_setting)(const char *key, const char *value)) {
    *s = (Settings){.scale = SCALE_FIT, .window_scale = 3, .record_misses = 1, .aspect_tv = 1};
    FILE *f = fopen(path, "r");
    if (!f) {
        if ((f = fopen(path, "w"))) {
            fputs(HEAD, f);
            fputs(sys_text, f);
            fputs(TAIL, f);
            fclose(f);
        }
        return;
    }
    char line[1024];
    for (int n = 1; fgets(line, sizeof line, f); n++) {
        char key[32], val[512];
        char *hash = strchr(line, '#');
        if (hash) *hash = 0;
        if (sscanf(line, " %31[a-z_] = %511[^\r\n]", key, val) != 2) {
            if (strspn(line, " \t\r\n") != strlen(line)) fprintf(stderr, "%s:%d: not 'key = value'\n", path, n);
            continue;
        }
        for (size_t len = strlen(val); len && (val[len - 1] == ' ' || val[len - 1] == '\t');) val[--len] = 0;
        int ok = 0;
        if (!strcmp(key, "fullscreen")) ok = settings_bool(val, &s->fullscreen);
        else if (!strcmp(key, "tv_frame")) ok = settings_bool(val, &s->tv_frame);
        else if (!strcmp(key, "record_misses")) ok = settings_bool(val, &s->record_misses);
        else if (!strcmp(key, "aspect")) {
            if (!strcmp(val, "tv")) s->aspect_tv = 1, ok = 1;
            else if (!strcmp(val, "square")) s->aspect_tv = 0, ok = 1;
        }
        else if (!strcmp(key, "scale")) {
            static const char *const NAMES[] = {"fit", "integer", "stretch"};
            for (int i = 0; i < 3; i++)
                if (!strcmp(val, NAMES[i])) s->scale = i, ok = 1;
        } else if (!strcmp(key, "window_scale")) {
            int k = atoi(val);
            if (k >= 1 && k <= 8) s->window_scale = k, ok = 1;
        } else {
            ok = sys_setting(key, val);
        }
        if (!ok) fprintf(stderr, "%s:%d: ignored '%s = %s'\n", path, n, key, val);
    }
    fclose(f);
}
