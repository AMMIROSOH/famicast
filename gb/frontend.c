/* The Game Boy for the SDL3 frontend (core/frontend/system.h): loading,
 * battery saves (ROM.sav: the cartridge RAM, then for MBC3 the clock and
 * when it was saved, so it moves on while the program is closed), the
 * screen's palette. No sound yet (G4). */
#include <SDL3/SDL.h>
#include <string.h>
#include <time.h>

#include "gb.h"
#include "system.h"

#ifndef APP_NAME
#define APP_NAME "gb"
#endif

const char SYS_SETTINGS[] =
    "# palette: the four shades: green = the original screen's tint, gray = neutral grays\n"
    "palette = green\n";

static int gray;

int sys_setting(const char *key, const char *val) {
    if (strcmp(key, "palette")) return 0;
    if (!strcmp(val, "green")) return gray = 0, 1;
    if (!strcmp(val, "gray")) return gray = 1, 1;
    return 0;
}

static Gb gb;
static uint8_t *rom;
static size_t rom_size;
static char sav_path[1024];
enum { RTC_BYTES = 5 + 5 + 8 };  /* the clock, as latched, the time of saving (seconds, little endian) */

static void load_sav(void) {
    size_t size;
    uint8_t *d = SDL_LoadFile(sav_path, &size);
    GbCart *c = &gb.cart;
    if (d && (size == c->ram_size || (c->has_rtc && size == c->ram_size + RTC_BYTES))) {
        memcpy(c->ram, d, c->ram_size);
        if (size > c->ram_size) {
            const uint8_t *r = d + c->ram_size;
            memcpy(c->rtc, r, 5);
            memcpy(c->rtc_latched, r + 5, 5);
            uint64_t then = 0;
            for (int i = 7; i >= 0; i--) then = then << 8 | r[10 + i];
            uint64_t now = (uint64_t)time(NULL);
            if (now > then) gb_cart_rtc_advance(c, now - then);
        }
    }
    SDL_free(d);
}

static void save_sav(void) {
    GbCart *c = &gb.cart;
    if (!c->battery || !c->ram_size) return;
    uint8_t d[sizeof c->ram + RTC_BYTES];
    size_t n = c->ram_size;
    memcpy(d, c->ram, n);
    if (c->has_rtc) {
        memcpy(d + n, c->rtc, 5);
        memcpy(d + n + 5, c->rtc_latched, 5);
        uint64_t now = (uint64_t)time(NULL);
        for (int i = 0; i < 8; i++) d[n + 10 + i] = (uint8_t)(now >> (8 * i));
        n += RTC_BYTES;
    }
    if (!SDL_SaveFile(sav_path, d, n)) SDL_Log("%s: %s", sav_path, SDL_GetError());
}

int sys_open(const char *path, const Settings *settings, SysInfo *info, char *err, size_t errlen) {
    (void)settings;
    if (!path) return SDL_snprintf(err, errlen, "usage: %s ROM [--frames N] [--record FILE] [--replay FILE]", APP_NAME), 2;
    rom = SDL_LoadFile(path, &rom_size);
    if (!rom) return SDL_snprintf(err, errlen, "%s: %s", path, SDL_GetError()), 1;
    char msg[128];
    if (gb_load(&gb, rom, rom_size, msg, sizeof msg) != 0) return SDL_snprintf(err, errlen, "%s: %s", path, msg), 1;
    SDL_snprintf(sav_path, sizeof sav_path, "%s.sav", path);
    SDL_snprintf(info->state_path, sizeof info->state_path, "%s.state", path);
    if (gb.cart.battery) load_sav();
    const char *name = path;
    for (const char *p = path; *p; p++)
        if (*p == '/' || *p == '\\') name = p + 1;
    SDL_strlcpy(info->name, name, sizeof info->name);
    SDL_strlcpy(info->tag, "Game Boy", sizeof info->tag);
    info->width = info->cols = 160;
    info->height = info->lines = 144;
    info->fps = 4194304.0 / 70224;  /* 59.73 Hz */
    info->aspect = 1.0;
    info->sample_rate = 48000;
    return 0;
}

/* The power button: the same game, its cartridge RAM and clock kept. */
void sys_reset(void) {
    static GbCart keep;
    keep = gb.cart;
    gb_free(&gb);
    char err[128];
    if (gb_load(&gb, rom, rom_size, err, sizeof err) != 0) return;
    memcpy(gb.cart.ram, keep.ram, sizeof keep.ram);
    memcpy(gb.cart.rtc, keep.rtc, sizeof keep.rtc);
}

int sys_frame(const InputFrame *in, uint32_t *rgb, float *samples, int max) {
    (void)samples, (void)max;
    static const uint32_t GREEN[4] = {0xE0F8D0, 0x88C070, 0x346856, 0x081820};
    static const uint32_t GRAY[4] = {0xFFFFFF, 0xAAAAAA, 0x555555, 0x000000};
    const uint32_t *pal = gray ? GRAY : GREEN;
    gb_set_pad(&gb, in->pad[0]);
    gb_run_frame(&gb);
    for (int i = 0; i < 160 * 144; i++) rgb[i] = pal[gb.frame[i] & 3];
    return 0;  /* no sound yet */
}

const char *sys_halted(char *buf, size_t len) {
    if (!gb.cpu.jammed) return NULL;
    SDL_snprintf(buf, len, "CPU locked up at $%04X\nF5 reset, F4 load state", (uint16_t)(gb.cpu.pc - 1));
    return buf;
}

int sys_state_save(const char *path) { return gb_state_save(&gb, path); }
int sys_state_load(const char *path, char *err, size_t errlen) { return gb_state_load(&gb, path, err, errlen); }
const char *sys_key(int key, char *buf, size_t len) { (void)key, (void)buf, (void)len; return NULL; }

void sys_close(void) {
    save_sav();
    gb_free(&gb);
    SDL_free(rom);
}
