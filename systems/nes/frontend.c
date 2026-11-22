/* The NES and Famicom Disk System for the SDL3 frontend (core/frontend/system.h):
 * loading (a ROM, the embedded one, the FDS BIOS), battery and disk saves
 * (ROM.sav), the palette, region and overscan settings, F6 = next disk
 * side, and the misses file of recompiled games (<program>.misses, read by
 * the build: CMakeLists.txt, nes_recompile). */
#include <SDL3/SDL.h>
#include <string.h>

#include "nes.h"
#include "recomp_rt.h"
#include "system.h"

#ifndef APP_NAME
#define APP_NAME "nes"
#endif

static const char SETTINGS[] =
    "# overscan: 1 = hide the top and bottom 8 lines, as a TV did (games\n"
    "# leave scrolling leftovers there that nobody saw); 0 = all 240 lines\n"
    "overscan = 1\n"
    "\n"
    "# overscan_sides: 1 = hide the left and right 8 columns too (TVs cut the\n"
    "# sides a little; scrolling games often blank the leftmost 8 themselves)\n"
    "overscan_sides = 0\n"
    "\n"
    "# sprite_limit: 1 = at most 8 sprites per line, as on the console (games\n"
    "# flicker to show them all); 0 = draw every sprite: less flicker, but a few\n"
    "# games hide things behind the limit on purpose\n"
    "sprite_limit = 1\n"
    "\n"
    "# region: the console to run as: auto = what the ROM says (NES 2.0 header),\n"
    "#         ntsc (USA/Japan, 60 Hz), pal (Europe, 50 Hz), dendy (Russian famiclone)\n"
    "#         Forcing a region the game wasn't made for: NTSC games on pal run\n"
    "#         slower but usually work; PAL-only games on ntsc often freeze or jam,\n"
    "#         as on a real NTSC console (shorter frame, much shorter vblank)\n"
    "region = auto\n"
    "\n"
    "# fds_bios: the Famicom Disk System BIOS (8 KB), needed for .fds disk images:\n"
    "# a file name next to the program or the disk image, or a full path\n"
    "fds_bios = disksys.rom\n"
    "\n"
    "# fds_auto_disk: 1 = swap disk sides when a game asks for another one;\n"
    "# 0 = only with F6 (next side)\n"
    "fds_auto_disk = 1\n";

static struct {
    int overscan, overscan_sides, sprite_limit, region, fds_auto_disk;
    char fds_bios[512];
} cfg = {1, 0, 1, -1, 1, "disksys.rom"};

static int sys_setting(const char *key, const char *val) {
    if (!strcmp(key, "overscan")) return settings_bool(val, &cfg.overscan);
    if (!strcmp(key, "overscan_sides")) return settings_bool(val, &cfg.overscan_sides);
    if (!strcmp(key, "sprite_limit")) return settings_bool(val, &cfg.sprite_limit);
    if (!strcmp(key, "fds_auto_disk")) return settings_bool(val, &cfg.fds_auto_disk);
    if (!strcmp(key, "fds_bios")) return strlen(val) < sizeof cfg.fds_bios && (strcpy(cfg.fds_bios, val), 1);
    if (!strcmp(key, "region")) {
        static const char *const NAMES[] = {"auto", "ntsc", "pal", "dendy"};
        static const int VALUES[] = {-1, REGION_NTSC, REGION_PAL, REGION_DENDY};
        for (int i = 0; i < 4; i++)
            if (!strcmp(val, NAMES[i])) return cfg.region = VALUES[i], 1;
    }
    return 0;
}

static Nes nes;
static uint32_t palette[512];
static int forced;       /* region forced in the settings */
static int jam_logged;
static size_t disk_size; /* FDS: all sides, as saved */
static char sav_path[1024], misses_path[1024];

/* The FDS BIOS (8 KB): `name` as a full path, else next to the program, else
 * next to the disk image. NULL if not found. */
static void *load_fds_bios(const char *name, const char *image_path) {
    char path[1536];
    const char *tries[3] = {NULL, NULL, NULL};
    int full = name[0] == '/' || name[0] == '\\' || (name[0] && name[1] == ':');
    if (full) tries[0] = name;
    else {
        static char a[1536], b[1536];
        SDL_snprintf(a, sizeof a, "%s%s", SDL_GetBasePath(), name);
        tries[0] = a;
        if (image_path) {
            SDL_snprintf(b, sizeof b, "%s", image_path);
            char *slash = SDL_strrchr(b, '/'), *back = SDL_strrchr(b, '\\');
            if (back > slash) slash = back;
            if (slash) slash[1] = 0;
            else b[0] = 0;
            SDL_snprintf(path, sizeof path, "%s%s", b, name);
            SDL_strlcpy(b, path, sizeof b);
            tries[1] = b;
        }
    }
    for (int i = 0; i < 3 && tries[i]; i++) {
        size_t size;
        void *data = SDL_LoadFile(tries[i], &size);
        if (data && size == 0x2000) return data;
        if (data) SDL_Log("%s: not an 8 KB FDS BIOS", tries[i]);
        SDL_free(data);
    }
    return NULL;
}

static int sys_open(const char *rom_path, const Settings *settings, SysInfo *info, char *err, size_t errlen) {
    /* FDS builds embed the BIOS; -DFDS_BIOS's fds (or EMBED_ROM=OFF) no disk */
    const int bios_build = recomp_program && recomp_program->bios && !recomp_program->rom;
    const int embedded = !rom_path && recomp_program && recomp_program->rom;
    if (!rom_path && bios_build) return SDL_snprintf(err, errlen, "Give a Famicom Disk System disk image (.fds) to play."), 1;
    if (!rom_path && !embedded) return SDL_snprintf(err, errlen, "usage: %s ROM [--frames N] [--record FILE] [--replay FILE]", APP_NAME), 2;
    size_t size = embedded ? recomp_program->rom_size : 0;
    void *rom = embedded ? NULL : SDL_LoadFile(rom_path, &size);
    if (!embedded && !rom) return SDL_snprintf(err, errlen, "%s: %s", rom_path, SDL_GetError()), 1;
    const uint8_t *image = embedded ? recomp_program->rom : rom;
    void *bios = NULL;
    if (recomp_program && recomp_program->bios) cart_fds_bios(recomp_program->bios);
    else if (cart_is_fds(image, size) && !(bios = load_fds_bios(cfg.fds_bios, rom_path))) {
        SDL_free(rom);
        return SDL_snprintf(err, errlen, "Famicom Disk System games need the FDS BIOS (8 KB): put it next to the program as "
                                         "disksys.rom, or set fds_bios in %s.ini", APP_NAME), 1;
    }
    if (bios) cart_fds_bios(bios);
    char msg[128];
    const char *name = embedded ? recomp_program->rom_name : rom_path;
    int failed = nes_load(&nes, image, size, msg, sizeof msg) != 0;
    SDL_free(rom);
    SDL_free(bios);  /* nes_load copied it */
    if (failed) return SDL_snprintf(err, errlen, "%s: %s", name, msg), 1;
    /* battery-backed PRG RAM (saves): ROM path + ".sav", 8 KB; for the
     * embedded ROM, its file name + ".sav" next to the program. FDS: what
     * the game wrote to the disk, all sides as on the disk */
    if (embedded) SDL_snprintf(sav_path, sizeof sav_path, "%s%s", SDL_GetBasePath(), recomp_program->rom_name);
    else SDL_snprintf(sav_path, sizeof sav_path, "%s", rom_path);
    SDL_snprintf(info->state_path, sizeof info->state_path, "%s.state", sav_path);
    SDL_strlcat(sav_path, ".sav", sizeof sav_path);
    for (const char *p = name; *p; p++)
        if (*p == '/' || *p == '\\') name = p + 1;
    SDL_strlcpy(info->name, name, sizeof info->name);
    forced = cfg.region >= 0 && cfg.region != nes.cart.region;
    if (forced) nes_set_region(&nes, cfg.region);
    ppu_all_sprites = !cfg.sprite_limit;
    SDL_snprintf(misses_path, sizeof misses_path, "%s%s.misses", SDL_GetBasePath(), APP_NAME);
    if (nes.recomp && settings->record_misses) nes.misses = SDL_calloc(nes_miss_slots(&nes), sizeof *nes.misses);
    if (nes.cart.battery) {
        size_t sav_size;
        void *sav = SDL_LoadFile(sav_path, &sav_size);
        if (sav && sav_size == nes.cart.prg_ram_size) SDL_memcpy(nes.cart.prg_ram, sav, sav_size);
        SDL_free(sav);
    }
    disk_size = nes.cart.disk ? (size_t)FDS_RAW_SIDE * nes.cart.disk_sides : 0;
    if (disk_size) {
        size_t sav_size;
        void *sav = SDL_LoadFile(sav_path, &sav_size);
        if (sav && sav_size == disk_size) SDL_memcpy(nes.cart.disk, sav, sav_size);
        SDL_free(sav);
        nes.cart.fds_auto = (uint8_t)cfg.fds_auto_disk;
    }
    nes_palette_rgb(palette);
    info->width = 256, info->height = 240;
    info->lines = cfg.overscan ? 240 - 16 : 240;
    info->cols = cfg.overscan_sides ? 256 - 16 : 256;
    /* 8:7 NTSC; PAL and Dendy pixels 1.3862:1, as a PAL TV showed them
     * (nesdev "Overscan", from memory) */
    info->aspect = nes.cart.region == REGION_NTSC ? 8.0 / 7 : 1.3862;
    info->fps = nes_frame_rate(&nes);
    info->sample_rate = APU_SAMPLE_RATE;
    info->channels = 1;
    SDL_strlcpy(info->tag, nes.cart.disk ? "FDS" : nes.cart.region == REGION_PAL ? "PAL"
                         : nes.cart.region == REGION_DENDY ? "Dendy" : "NTSC", sizeof info->tag);
    return 0;
}

static void sys_reset(void) { nes_reset(&nes); }

static int sys_frame(const InputFrame *in, uint32_t *rgb, float *samples, int max) {
    nes.pad[0] = (uint8_t)in->pad[0];  /* the NES pad: the low byte */
    nes.pad[1] = (uint8_t)in->pad[1];
    nes_run_frame(&nes);
    for (int i = 0; i < 256 * 240; i++) rgb[i] = palette[nes.ppu.fb[i] & 0x1FF];
    return apu_take_samples(&nes.apu, samples, max);
}

/* A KIL opcode halts a real 6502 until reset while the TV keeps the last
 * picture: the window stays open on it, so F5 or F4 can recover.
 * shortcut: the PPU and APU stop too (on hardware they run on, so a held
 * note drones); fine for a game that is dead anyway */
static const char *sys_halted(char *buf, size_t len) {
    if (!nes.cpu.jammed) return jam_logged = 0, NULL;
    if (!jam_logged) SDL_Log("CPU jammed at $%04X", nes.cpu.pc), jam_logged = 1;
    SDL_snprintf(buf, len, "CPU jammed at $%04X\nF5 reset, F4 load state%s", nes.cpu.pc,
                 forced ? "\n(region forced in " APP_NAME ".ini)" : "");
    return buf;
}

static int sys_state_save(const char *path) { return nes_state_save(&nes, path); }
static int sys_state_load(const char *path, char *err, size_t errlen) { return nes_state_load(&nes, path, err, errlen); }

static const char *sys_key(int key, char *buf, size_t len) {
    if (key != SDLK_F6 || !nes.cart.disk) return NULL;
    /* FDS: the next side, after half a second out */
    int cur = nes.cart.disk_side >= 0 ? nes.cart.disk_side : nes.cart.disk_next;
    int side = (cur + 1) % nes.cart.disk_sides;
    cart_fds_insert(&nes.cart, side, nes.cpu.cycles + 900000);
    SDL_snprintf(buf, len, "disk %d side %c", side / 2 + 1, 'A' + side % 2);
    return buf;
}

static void sys_close(void) {
    if (nes.cart.battery && !SDL_SaveFile(sav_path, nes.cart.prg_ram, nes.cart.prg_ram_size))
        SDL_Log("%s: %s", sav_path, SDL_GetError());
    if (disk_size && nes.cart.disk_dirty && !SDL_SaveFile(sav_path, nes.cart.disk, disk_size))
        SDL_Log("%s: %s", sav_path, SDL_GetError());
    /* rewrite the misses file only when this session found new ROM code, so
     * the next build does not rerun the recompiler for nothing */
    if (nes.misses && nes_misses_merge(&nes, misses_path) != 0) {
        FILE *f = fopen(misses_path, "w");
        if (f) nes_misses_write(&nes, f), fclose(f);
        else SDL_Log("%s: cannot write", misses_path);
    }
    SDL_free(nes.misses);
    cart_free(&nes.cart);
}

extern const SystemDetector nes_detector;
const System nes_system = {&nes_detector, APP_NAME, SETTINGS, sys_setting, sys_open, sys_reset, sys_frame,
    sys_halted, sys_state_save, sys_state_load, sys_key, sys_close};
