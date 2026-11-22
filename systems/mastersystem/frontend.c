/* The Master System behind core/frontend/system.h: ROM loading, cartridge
 * RAM kept in ROM.sav next to the game, the region, and the pads (button 1 =
 * B, button 2 = A, Start = the console's Pause button). */
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "input_log.h"
#include "loader.h"
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
static char sav_path[1024];

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

static int sys_open(const char *path, const Settings *settings, SysInfo *info, char *err, size_t errlen) {
    (void)settings;
    if (!path) { snprintf(err, errlen, "usage: retrocast game.sms"); return 1; }
    if (sms_load_rom(path, &rom, &rom_size, err, errlen)) return 1;
    int pal = region < 0 ? strstr(path, "(Europe)") != NULL : region;
    sms_init(&machine, rom, rom_size, pal);
    snprintf(sav_path, sizeof sav_path, "%s.sav", path);
    size_t n = 0;
    uint8_t *sav = SDL_LoadFile(sav_path, &n);
    if (sav && n == sizeof machine.cart.ram) memcpy(machine.cart.ram, sav, n), machine.cart.ram_used = 1;
    SDL_free(sav);
    const char *name = path;
    for (const char *p = path; *p; p++)
        if (*p == '/' || *p == '\\') name = p + 1;
    snprintf(info->name, sizeof info->name, "%s", name);
    snprintf(info->tag, sizeof info->tag, "%s", pal ? "PAL" : "NTSC");
    snprintf(info->state_path, sizeof info->state_path, "%s.state", path);
    info->width = info->cols = SMS_WIDTH;
    info->height = info->lines = SMS_HEIGHT;
    info->fps = (double)(pal ? SMS_CLOCK_PAL : SMS_CLOCK_NTSC) / (sms_lines(&machine) * SMS_LINE_TSTATES);
    info->aspect = 8.0 / 7.0;  /* the NTSC pixel; close enough for PAL's */
    info->sample_rate = SMS_SAMPLE_RATE;
    info->channels = 1;
    return 0;
}

static void sys_reset(void) { sms_reset(&machine); }

static int sys_frame(const InputFrame *in, uint32_t *rgb, float *samples, int max) {
    sms_frame(&machine, in->pad[0], in->pad[1]);
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
    free(rom);
    rom = NULL;
}

extern const SystemDetector sms_detector;
const System mastersystem_system = {&sms_detector, APP_NAME, SETTINGS, sys_setting, sys_open, sys_reset, sys_frame,
                                    sys_halted, sys_state_save, sys_state_load, sys_key, sys_close};
