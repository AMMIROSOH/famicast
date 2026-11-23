/* The Master System and the Game Gear behind core/frontend/system.h: ROM
 * loading, cartridge RAM kept in ROM.sav next to the game, the region, and
 * the pads (button 1 = B, button 2 = A, Start = the Master System's Pause
 * button, the Game Gear's Start). A game runs in Game Gear mode when
 * sms_gg_mode says so, or always as gamegear_system (--system gg); the
 * window then shows the Game Gear's 160x144 LCD. */
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "input_log.h"
#include "loader.h"
#include "recomp_rt.h"
#include "sms.h"
#include "system.h"

#ifndef APP_NAME
#define APP_NAME "mastersystem"
#endif

static const char SETTINGS[] =
    "region = auto       # auto (Europe in the file name: PAL), ntsc or pal\n";
static int region = -1;
static Sms machine;
static uint8_t *rom;
static size_t rom_size;
static char sav_path[1024], misses_path[1024];

static int sys_setting(const char *key, const char *value) {
    if (strcmp(key, "region")) return 0;
    if (!strcmp(value, "auto")) region = -1;
    else if (!strcmp(value, "ntsc")) region = 0;
    else if (!strcmp(value, "pal")) region = 1;
    else return 0;
    return 1;
}

static void save_sav(void) {
    if (machine.cart.ram_used && !SDL_SaveFile(sav_path, machine.cart.ram, sizeof machine.cart.ram))
        SDL_Log("%s: %s", sav_path, SDL_GetError());
}

static int open_game(const char *path, const Settings *settings, SysInfo *info, char *err, size_t errlen, int gg) {
    const int embedded = !path && sms_recomp_program && sms_recomp_program->rom;
    if (!path && !embedded) { snprintf(err, errlen, "usage: %s game.sms", APP_NAME); return 1; }
    if (embedded) {  /* a recompiled game: saves and states next to the program, by the ROM's name */
        rom_size = sms_recomp_program->rom_size;
        if (!(rom = malloc(rom_size))) { snprintf(err, errlen, "out of memory"); return 1; }
        memcpy(rom, sms_recomp_program->rom, rom_size);
        const char *name = sms_recomp_program->rom_name;
        for (const char *p = name; *p; p++)
            if (*p == '/' || *p == '\\') name = p + 1;
        snprintf(sav_path, sizeof sav_path, "%s%s.sav", SDL_GetBasePath(), name);
        path = sms_recomp_program->rom_name;
    } else {
        if (sms_load_rom(path, &rom, &rom_size, err, errlen)) return 1;
        snprintf(sav_path, sizeof sav_path, "%s.sav", path);
    }
    int pal = region < 0 ? strstr(path, "(Europe)") != NULL : region;
    sms_init(&machine, rom, rom_size, pal, gg || sms_gg_mode(rom, rom_size, path));
    pal = machine.pal;  /* the Game Gear is 60 Hz only */
    /* a recompiled game notes the ROM code it interpreted in <program>.misses (read by the next build) */
    snprintf(misses_path, sizeof misses_path, "%s%s.misses", SDL_GetBasePath(), APP_NAME);
    if (machine.recomp_on && settings->record_misses) machine.misses = calloc(sms_miss_slots(&machine), 1);
    size_t n = 0;
    uint8_t *sav = SDL_LoadFile(sav_path, &n);
    if (sav && n == sizeof machine.cart.ram) memcpy(machine.cart.ram, sav, n), machine.cart.ram_used = 1;
    SDL_free(sav);
    const char *name = path;
    for (const char *p = path; *p; p++)
        if (*p == '/' || *p == '\\') name = p + 1;
    snprintf(info->name, sizeof info->name, "%s", name);
    snprintf(info->tag, sizeof info->tag, "%s", pal ? "PAL" : "NTSC");
    snprintf(info->state_path, sizeof info->state_path, "%.*s.state", (int)(strlen(sav_path) - 4), sav_path);
    info->width = info->cols = machine.vdp.gg ? GG_WIDTH : SMS_WIDTH;
    info->height = info->lines = machine.vdp.gg ? GG_HEIGHT : SMS_HEIGHT;
    info->fps = (double)(pal ? SMS_CLOCK_PAL : SMS_CLOCK_NTSC) / (sms_lines(&machine) * SMS_LINE_TSTATES);
    info->aspect = 8.0 / 7.0;  /* the NTSC pixel; close enough for PAL's */
    info->sample_rate = SMS_SAMPLE_RATE;
    info->channels = 1;
    return 0;
}

static int sys_open(const char *path, const Settings *settings, SysInfo *info, char *err, size_t errlen) {
    return open_game(path, settings, info, err, errlen, 0);
}
static int gg_open(const char *path, const Settings *settings, SysInfo *info, char *err, size_t errlen) {
    return open_game(path, settings, info, err, errlen, 1);
}

static void sys_reset(void) { sms_reset(&machine); }

static int sys_frame(const InputFrame *in, uint32_t *rgb, float *samples, int max) {
    sms_frame(&machine, in->pad[0], in->pad[1]);
    if (machine.vdp.gg)
        for (unsigned y = 0; y < GG_HEIGHT; y++)
            for (unsigned x = 0; x < GG_WIDTH; x++)
                rgb[y * GG_WIDTH + x] = sms_color(&machine, machine.vdp.frame[(y + GG_Y) * SMS_WIDTH + x + GG_X]);
    else
        for (unsigned i = 0; i < SMS_WIDTH * SMS_HEIGHT; i++) rgb[i] = sms_color(&machine, machine.vdp.frame[i]);
    int n = (int)machine.n_samples < max ? (int)machine.n_samples : max;
    memcpy(samples, machine.samples, (size_t)n * sizeof *samples);
    return n;
}

static const char *sys_halted(char *buf, size_t len) { (void)buf, (void)len; return NULL; }  /* a Z80 cannot jam */
static int sys_state_save(const char *path) { return sms_save(&machine, path); }
static int sys_state_load(const char *path, char *err, size_t len) { return sms_load(&machine, path, err, len) ? -1 : 0; }
static const char *sys_key(int key, char *buf, size_t len) { (void)key, (void)buf, (void)len; return NULL; }
static void sys_close(void) {
    save_sav();
    /* rewrite the misses file only when this session found new code, so the
     * next build does not rerun the recompiler for nothing */
    if (machine.misses && sms_misses_merge(&machine, misses_path)) {
        FILE *f = fopen(misses_path, "w");
        if (f) sms_misses_write(&machine, f), fclose(f);
        else SDL_Log("%s: cannot write", misses_path);
    }
    free(machine.misses);
    machine.misses = NULL;
    free(rom);
    rom = NULL;
}

extern const SystemDetector sms_detector, gg_detector;
const System mastersystem_system = {&sms_detector, APP_NAME, SETTINGS, sys_setting, sys_open, sys_reset, sys_frame,
                                    sys_halted, sys_state_save, sys_state_load, sys_key, sys_close};
/* settings in gamegear.ini; only in retrocast (a recompiled game has one system) */
const System gamegear_system = {&gg_detector, "gamegear", SETTINGS, sys_setting, gg_open, sys_reset, sys_frame,
                                sys_halted, sys_state_save, sys_state_load, sys_key, sys_close};
