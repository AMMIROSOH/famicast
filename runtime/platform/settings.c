#include "settings.h"

#include "../cart.h"  /* REGION_* */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char DEFAULT_FILE[] =
    "# famicast settings. Edit, then restart the game.\n"
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
    "# overscan: 1 = hide the top and bottom 8 lines, as a TV did (games\n"
    "# leave scrolling leftovers there that nobody saw); 0 = all 240 lines\n"
    "overscan = 1\n"
    "\n"
    "# aspect: tv = pixels the shape a TV gave them (NTSC 8:7, PAL games 1.39:1);\n"
    "#         square = square pixels (the picture 12% narrower, PAL 28%)\n"
    "aspect = tv\n"
    "\n"
    "# region: the console to run as: auto = what the ROM says (NES 2.0 header),\n"
    "#         ntsc (USA/Japan, 60 Hz), pal (Europe, 50 Hz), dendy (Russian famiclone)\n"
    "#         Forcing a region the game wasn't made for: NTSC games on pal run\n"
    "#         slower but usually work; PAL-only games on ntsc often freeze or jam,\n"
    "#         as on a real NTSC console (shorter frame, much shorter vblank)\n"
    "region = auto\n"
    "\n"
    "# window_scale: starting window size, in multiples of the picture (1-8)\n"
    "window_scale = 3\n"
    "\n"
    "# record_misses: 1 = remember game code that ran in the interpreter because\n"
    "# the recompiler had not found it, in <program>.misses next to the program;\n"
    "# the next build of this game recompiles it (recompiled games only)\n"
    "record_misses = 1\n";

static int parse_bool(const char *v, int *out) {
    if (!strcmp(v, "1") || !strcmp(v, "on") || !strcmp(v, "yes") || !strcmp(v, "true")) return *out = 1, 1;
    if (!strcmp(v, "0") || !strcmp(v, "off") || !strcmp(v, "no") || !strcmp(v, "false")) return *out = 0, 1;
    return 0;
}

void settings_load(Settings *s, const char *path) {
    *s = (Settings){0, SCALE_FIT, 0, 3, 1, 1, 1, -1};
    FILE *f = fopen(path, "r");
    if (!f) {
        if ((f = fopen(path, "w"))) {
            fputs(DEFAULT_FILE, f);
            fclose(f);
        }
        return;
    }
    char line[256];
    for (int n = 1; fgets(line, sizeof line, f); n++) {
        char key[32], val[32];
        char *hash = strchr(line, '#');
        if (hash) *hash = 0;
        if (sscanf(line, " %31[a-z_] = %31s", key, val) != 2) {
            if (strspn(line, " \t\r\n") != strlen(line)) fprintf(stderr, "%s:%d: not 'key = value'\n", path, n);
            continue;
        }
        int ok = 0;
        if (!strcmp(key, "fullscreen")) ok = parse_bool(val, &s->fullscreen);
        else if (!strcmp(key, "tv_frame")) ok = parse_bool(val, &s->tv_frame);
        else if (!strcmp(key, "record_misses")) ok = parse_bool(val, &s->record_misses);
        else if (!strcmp(key, "overscan")) ok = parse_bool(val, &s->overscan);
        else if (!strcmp(key, "aspect")) {
            if (!strcmp(val, "tv")) s->aspect_tv = 1, ok = 1;
            else if (!strcmp(val, "square")) s->aspect_tv = 0, ok = 1;
        }
        else if (!strcmp(key, "region")) {
            static const char *const NAMES[] = {"auto", "ntsc", "pal", "dendy"};
            static const int VALUES[] = {-1, REGION_NTSC, REGION_PAL, REGION_DENDY};
            for (int i = 0; i < 4; i++)
                if (!strcmp(val, NAMES[i])) s->region = VALUES[i], ok = 1;
        }
        else if (!strcmp(key, "scale")) {
            static const char *const NAMES[] = {"fit", "integer", "stretch"};
            for (int i = 0; i < 3; i++)
                if (!strcmp(val, NAMES[i])) s->scale = i, ok = 1;
        } else if (!strcmp(key, "window_scale")) {
            int k = atoi(val);
            if (k >= 1 && k <= 8) s->window_scale = k, ok = 1;
        }
        if (!ok) fprintf(stderr, "%s:%d: ignored '%s = %s'\n", path, n, key, val);
    }
    fclose(f);
}
