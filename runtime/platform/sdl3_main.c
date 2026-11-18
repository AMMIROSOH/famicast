/* SDL3 frontend: runs a ROM (recompiled code where linked, else interpreted).
 * Usage: nes ROM [--frames N] [--record FILE] [--replay FILE]
 *   ROM may be left out in recompiled builds: the ROM they embed is used.
 *   --frames N   quit after N frames (smoke tests)
 *   --record     save the input of every frame (runtime/input_log.h)
 *   --replay     play a recording back; live input resumes when it ends
 * Keyboard (player 1): arrows, X = A, Z = B, Enter = Start, Right Shift =
 * Select, F5 = reset, Esc = quit. F2 = save state, F4 = load state,
 * F6 = next disk side (FDS), F10 = TV frame on/off, F11 or Alt+Enter =
 * fullscreen on/off. FDS disk images need the BIOS (fds_bios in the .ini).
 * Display settings: <program>.ini next to the program (settings.c).
 * Recompiled games note ROM code that had to be interpreted in
 * <program>.misses; the build reads it (CMakeLists.txt, nes_recompile).
 * Gamepads: first connected = player 1, second = player 2; hot-plug works.
 * D-pad or left stick; East = A, South = B (NES layout: B left of A),
 * Back = Select, Start = Start.
 * Battery saves are kept next to the ROM as ROM.sav, save states as
 * ROM.state (for an embedded ROM: next to the program). */
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "input_log.h"
#include "nes.h"
#include "recomp_rt.h"
#include "settings.h"
#include "tv_frame.h"

#ifndef NES_APP_NAME
#define NES_APP_NAME "nes"  /* CMake sets the game's name: the settings file is NAME.ini */
#endif

enum { NES_W = 256, NES_H = 240, STICK_DEADZONE = 16000 };

static SDL_Gamepad *gamepads[2];

static uint8_t keyboard_buttons(void) {
    const bool *k = SDL_GetKeyboardState(NULL);
    uint8_t b = 0;
    if (k[SDL_SCANCODE_X]) b |= PAD_A;
    if (k[SDL_SCANCODE_Z]) b |= PAD_B;
    if (k[SDL_SCANCODE_RSHIFT]) b |= PAD_SELECT;
    if (k[SDL_SCANCODE_RETURN] && !(SDL_GetModState() & SDL_KMOD_ALT)) b |= PAD_START;  /* Alt+Enter: fullscreen */
    if (k[SDL_SCANCODE_UP]) b |= PAD_UP;
    if (k[SDL_SCANCODE_DOWN]) b |= PAD_DOWN;
    if (k[SDL_SCANCODE_LEFT]) b |= PAD_LEFT;
    if (k[SDL_SCANCODE_RIGHT]) b |= PAD_RIGHT;
    return b;
}

static uint8_t gamepad_buttons(SDL_Gamepad *g) {
    if (!g) return 0;
    static const struct { SDL_GamepadButton button; uint8_t bit; } map[] = {
        {SDL_GAMEPAD_BUTTON_EAST, PAD_A},       {SDL_GAMEPAD_BUTTON_SOUTH, PAD_B},
        {SDL_GAMEPAD_BUTTON_BACK, PAD_SELECT},  {SDL_GAMEPAD_BUTTON_START, PAD_START},
        {SDL_GAMEPAD_BUTTON_DPAD_UP, PAD_UP},   {SDL_GAMEPAD_BUTTON_DPAD_DOWN, PAD_DOWN},
        {SDL_GAMEPAD_BUTTON_DPAD_LEFT, PAD_LEFT}, {SDL_GAMEPAD_BUTTON_DPAD_RIGHT, PAD_RIGHT},
    };
    uint8_t b = 0;
    for (size_t i = 0; i < sizeof map / sizeof *map; i++)
        if (SDL_GetGamepadButton(g, map[i].button)) b |= map[i].bit;
    int x = SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_LEFTX);
    int y = SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_LEFTY);
    if (x < -STICK_DEADZONE) b |= PAD_LEFT;
    if (x > STICK_DEADZONE) b |= PAD_RIGHT;
    if (y < -STICK_DEADZONE) b |= PAD_UP;
    if (y > STICK_DEADZONE) b |= PAD_DOWN;
    return b;
}

static void gamepad_event(const SDL_Event *e) {
    if (e->type == SDL_EVENT_GAMEPAD_ADDED) {
        for (int i = 0; i < 2; i++)
            if (gamepads[i] && SDL_GetGamepadID(gamepads[i]) == e->gdevice.which) return;
        for (int i = 0; i < 2; i++) {
            if (gamepads[i]) continue;
            gamepads[i] = SDL_OpenGamepad(e->gdevice.which);
            if (gamepads[i]) SDL_Log("player %d: %s", i + 1, SDL_GetGamepadName(gamepads[i]));
            return;
        }
    } else if (e->type == SDL_EVENT_GAMEPAD_REMOVED) {
        for (int i = 0; i < 2; i++) {
            if (gamepads[i] && SDL_GetGamepadID(gamepads[i]) == e->gdevice.which) {
                SDL_Log("player %d: gamepad disconnected", i + 1);
                SDL_CloseGamepad(gamepads[i]);
                gamepads[i] = NULL;
            }
        }
    }
}

static SDL_Texture *make_tv_frame(SDL_Renderer *ren, int width, int lines) {
    const int w = tv_w(width) * TV_RES, h = tv_h(lines) * TV_RES;
    uint8_t *px = SDL_malloc((size_t)w * h * 4);
    tv_frame_render(px, width, lines);
    SDL_Texture *t = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, w, h);
    if (t) {
        SDL_UpdateTexture(t, NULL, px, w * 4);
        SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(t, SDL_SCALEMODE_LINEAR);
    }
    SDL_free(px);
    return t;
}

/* Draw the picture's visible columns and lines (256 x 240, or 240 and 224
 * with the overscan hidden: centred), `width` units wide (256 square pixels, 293 for 8:7, 355 for PAL), and the
 * frame over it, scaled into the output. Integer scaling applies to the
 * lines; with 8:7 pixels the width follows (the pixel-art scale mode keeps
 * the uneven columns sharp). banner: NULL-terminated lines of text drawn
 * over the picture in SDL's built-in 8x8 font, one font pixel per NES pixel. */
static void present(SDL_Renderer *ren, SDL_Texture *pic, SDL_Texture *frame, int scale, int width, int cols,
                    int lines, const char *const *banner) {
    int W, H;
    SDL_GetCurrentRenderOutputSize(ren, &W, &H);
    float cw = (float)(frame ? tv_w(width) : width), ch = (float)(frame ? tv_h(lines) : lines);  /* content, in units */
    float sx = W / cw, sy = H / ch;
    if (scale != SCALE_STRETCH) {
        sx = sy = SDL_min(sx, sy);
        if (scale == SCALE_INTEGER && sx >= 1) sx = sy = SDL_floorf(sx);
    }
    SDL_FRect content = {SDL_floorf((W - cw * sx) / 2), SDL_floorf((H - ch * sy) / 2), cw * sx, ch * sy};
    SDL_FRect picture = {content.x + (frame ? TV_LEFT * sx : 0), content.y + (frame ? TV_TOP * sy : 0),
                         width * sx, lines * sy};
    SDL_FRect src = {(NES_W - cols) / 2.0f, (NES_H - lines) / 2.0f, (float)cols, (float)lines};
    SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
    SDL_RenderClear(ren);
    SDL_RenderTexture(ren, pic, &src, &picture);
    if (frame) SDL_RenderTexture(ren, frame, NULL, &content);
    const float k = SDL_max(1.0f, SDL_floorf(sy));
    SDL_SetRenderScale(ren, k, k);
    for (int i = 0; banner && banner[i]; i++) {
        float x = picture.x / k + 16, y = picture.y / k + 16 + i * 12;
        SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
        SDL_RenderFillRect(ren, &(SDL_FRect){x - 2, y - 2, SDL_strlen(banner[i]) * 8.0f + 4, 12});
        SDL_SetRenderDrawColor(ren, 255, 255, 255, 255);
        SDL_RenderDebugText(ren, x, y, banner[i]);
    }
    SDL_SetRenderScale(ren, 1, 1);
    SDL_RenderPresent(ren);
}

/* The emulator runs on its own thread, paced to the game's frame rate and
 * feeding the audio. The main thread only handles the window, events and
 * input: when Windows holds it (title bar dragged, a gamepad being
 * plugged in) the picture stops for a moment but the game and its sound go on. */
typedef struct {
    Nes *nes;
    SDL_Mutex *lock;          /* nes, fb, fb_new, jam_pc */
    uint32_t fb[NES_W * NES_H], palette[512];
    int fb_new, jam_pc;       /* jam_pc: -1, or where the CPU jammed */
    SDL_AtomicInt pads, reset, quit, done;  /* pads: player 1 | player 2 << 8 */
    SDL_AudioStream *audio;
    double fps;
    long max_frames;
    InputLog *replay;
    FILE *record;
} Emu;

static int emu_thread(void *arg) {
    Emu *u = arg;
    Nes *nes = u->nes;
    static float samples[APU_BUF];
    const double frame_ns = 1e9 / u->fps;
    uint64_t next = SDL_GetTicksNS();
    for (long frame = 0; !SDL_GetAtomicInt(&u->quit) && (u->max_frames <= 0 || frame < u->max_frames); frame++) {
        InputFrame in;
        int reset = SDL_SetAtomicInt(&u->reset, 0);
        if (frame < u->replay->count) {
            in = u->replay->frames[frame];
        } else {
            int pads = SDL_GetAtomicInt(&u->pads);
            in.pad[0] = (uint8_t)pads, in.pad[1] = (uint8_t)(pads >> 8);
            in.flags = reset ? INPUT_RESET : 0;
        }
        if (u->record) input_log_write(u->record, in);

        SDL_LockMutex(u->lock);
        if (in.flags & INPUT_RESET) nes_reset(nes);
        nes->pad[0] = in.pad[0];
        nes->pad[1] = in.pad[1];
        nes_run_frame(nes);
        int ns = apu_take_samples(&nes->apu, samples, APU_BUF);
        for (int i = 0; i < NES_W * NES_H; i++) u->fb[i] = u->palette[nes->ppu.fb[i] & 0x1FF];
        u->fb_new = 1;
        /* A KIL opcode halts a real 6502 until reset while the TV keeps the
         * last picture: the window stays open on it, so F5 or F4 can recover.
         * shortcut: the PPU and APU stop too (on hardware they run on, so a
         * held note drones); fine for a game that is dead anyway */
        int jam = nes->cpu.jammed ? nes->cpu.pc : -1;
        if (jam >= 0 && u->jam_pc < 0) SDL_Log("CPU jammed at $%04X", jam);
        u->jam_pc = jam;
        SDL_UnlockMutex(u->lock);

        if (u->audio) {
            SDL_PutAudioStreamData(u->audio, samples, ns * (int)sizeof(float));
            const float target = 3.0f * APU_SAMPLE_RATE / (float)u->fps;
            float queued = (float)SDL_GetAudioStreamQueued(u->audio) / sizeof(float);
            float ratio = 1.0f + 0.005f * (queued - target) / target;
            SDL_SetAudioStreamFrequencyRatio(u->audio, ratio < 0.995f ? 0.995f : ratio > 1.005f ? 1.005f : ratio);
        }
        next += (uint64_t)frame_ns;  /* pace to the game's rate regardless of monitor rate */
        uint64_t now = SDL_GetTicksNS();
        if (next > now) SDL_DelayPrecise(next - now);
        else {  /* fell behind: don't try to catch up */
            if (now - next > 3 * (uint64_t)frame_ns)  /* the audio queue ran dry */
                SDL_Log("frame %ld: %.0f ms late", frame, (now - next) / 1e6);
            next = now;
        }
    }
    SDL_SetAtomicInt(&u->done, 1);
    return 0;
}

/* A start-up error: logged, and shown in a box (the program has no console
 * when started from a file manager). Returns the exit code. */
static int fail(const char *fmt, ...) {
    char msg[1024];
    va_list ap;
    va_start(ap, fmt);
    SDL_vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    SDL_Log("%s", msg);
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, NES_APP_NAME, msg, NULL);
    return 1;
}

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

int main(int argc, char **argv) {
    const char *rom_path = argc > 1 && argv[1][0] != '-' ? argv[1] : NULL;
    /* -DFDS_BIOS builds embed the FDS BIOS (8 KB; no iNES file is that size) */
    const int bios_build = recomp_program && recomp_program->rom && recomp_program->rom_size == 0x2000;
    const int embedded = !rom_path && recomp_program && recomp_program->rom && !bios_build;
    if (!rom_path && bios_build) return fail("Give a Famicom Disk System disk image (.fds) to play.");
    if (!rom_path && !embedded) {
        SDL_Log("usage: nes ROM [--frames N] [--record FILE] [--replay FILE]");
        return 2;
    }
    long max_frames = 0;
    const char *record = NULL, *replay = NULL;
    for (int i = rom_path ? 2 : 1; i + 1 < argc; i++) {
        if (!strcmp(argv[i], "--frames")) max_frames = atol(argv[++i]);
        else if (!strcmp(argv[i], "--record")) record = argv[++i];
        else if (!strcmp(argv[i], "--replay")) replay = argv[++i];
    }

    size_t size = embedded ? recomp_program->rom_size : 0;
    void *rom = embedded ? NULL : SDL_LoadFile(rom_path, &size);
    if (!embedded && !rom) {
        SDL_Log("%s: %s", rom_path, SDL_GetError());
        return 1;
    }
    char ini_path[1024];
    SDL_snprintf(ini_path, sizeof ini_path, "%s%s.ini", SDL_GetBasePath(), NES_APP_NAME);
    Settings cfg;
    settings_load(&cfg, ini_path);
    const uint8_t *image = embedded ? recomp_program->rom : rom;
    void *bios = NULL;
    if (bios_build) cart_fds_bios(recomp_program->rom);
    else if (cart_is_fds(image, size) && !(bios = load_fds_bios(cfg.fds_bios, rom_path)))
        return fail("Famicom Disk System games need the FDS BIOS (8 KB): put it next to the program as "
                    "disksys.rom, or set fds_bios in %s", ini_path);
    if (bios) cart_fds_bios(bios);
    static Nes nes;
    char err[128];
    if (nes_load(&nes, image, size, err, sizeof err) != 0)
        return fail("%s: %s", embedded ? recomp_program->rom_name : rom_path, err);
    SDL_free(rom);
    SDL_free(bios);  /* nes_load copied it */
    /* battery-backed PRG RAM (saves): ROM path + ".sav", 8 KB; for the
     * embedded ROM, its file name + ".sav" next to the program. FDS: what
     * the game wrote to the disk, all sides as on the disk */
    char sav_path[1024], state_path[1024], title[512];
    const char *name = embedded ? recomp_program->rom_name : rom_path;
    if (embedded) SDL_snprintf(sav_path, sizeof sav_path, "%s%s", SDL_GetBasePath(), recomp_program->rom_name);
    else SDL_snprintf(sav_path, sizeof sav_path, "%s", rom_path);
    SDL_snprintf(state_path, sizeof state_path, "%s.state", sav_path);
    SDL_strlcat(sav_path, ".sav", sizeof sav_path);
    for (const char *p = name; *p; p++)
        if (*p == '/' || *p == '\\') name = p + 1;
    const int forced = cfg.region >= 0 && cfg.region != nes.cart.region;
    if (forced) nes_set_region(&nes, cfg.region);
    ppu_all_sprites = !cfg.sprite_limit;
    char misses_path[1024];
    SDL_snprintf(misses_path, sizeof misses_path, "%s%s.misses", SDL_GetBasePath(), NES_APP_NAME);
    if (nes.recomp && cfg.record_misses) nes.misses = SDL_calloc(nes_miss_slots(&nes), sizeof *nes.misses);
    if (nes.cart.battery) {
        size_t sav_size;
        void *sav = SDL_LoadFile(sav_path, &sav_size);
        if (sav && sav_size == nes.cart.prg_ram_size) SDL_memcpy(nes.cart.prg_ram, sav, sav_size);
        SDL_free(sav);
    }
    const size_t disk_size = nes.cart.disk ? (size_t)FDS_RAW_SIDE * nes.cart.disk_sides : 0;
    if (disk_size) {
        size_t sav_size;
        void *sav = SDL_LoadFile(sav_path, &sav_size);
        if (sav && sav_size == disk_size) SDL_memcpy(nes.cart.disk, sav, sav_size);
        SDL_free(sav);
        nes.cart.fds_auto = (uint8_t)cfg.fds_auto_disk;
    }

    InputLog replay_log = {0};
    if (replay && input_log_load(replay, &replay_log) != 0) {
        SDL_Log("%s: not an input recording", replay);
        return 1;
    }
    FILE *record_file = NULL;
    if (record && !(record_file = input_log_create(record))) {
        SDL_Log("%s: cannot create", record);
        return 1;
    }

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD | SDL_INIT_AUDIO)) {
        SDL_Log("SDL_Init: %s", SDL_GetError());
        return 1;
    }
    SDL_Window *win;
    SDL_Renderer *ren;
    /* starting size: window_scale x the content, smaller if the screen is */
    const int lines = cfg.overscan ? NES_H - 16 : NES_H;  /* visible lines */
    /* tv: 8:7 (256 columns: 292.6, rounded); PAL and Dendy pixels 1.3862:1 (354.9), as a PAL TV
     * showed them (nesdev "Overscan", from memory) */
    const int cols = cfg.overscan_sides ? NES_W - 16 : NES_W;  /* visible columns */
    const int width = !cfg.aspect_tv ? cols : (int)(cols * (nes.cart.region == REGION_NTSC ? 8.0 / 7 : 1.3862) + 0.5);
    int k = cfg.window_scale, cw = cfg.tv_frame ? tv_w(width) : width, ch = cfg.tv_frame ? tv_h(lines) : lines;
    SDL_Rect usable;
    if (SDL_GetDisplayUsableBounds(SDL_GetPrimaryDisplay(), &usable))
        while (k > 1 && (cw * k > usable.w || ch * k > usable.h - 40)) k--;
    /* the title: the game and the console it runs as, e.g. "Game (PAL)" */
    char heading[300];
    SDL_snprintf(heading, sizeof heading, "%s (%s)", name,
                 nes.cart.disk ? "FDS" : nes.cart.region == REGION_PAL ? "PAL" : nes.cart.region == REGION_DENDY ? "Dendy" : "NTSC");
    SDL_snprintf(title, sizeof title, "%s", heading);
    if (!SDL_CreateWindowAndRenderer(title, cw * k, ch * k, SDL_WINDOW_RESIZABLE, &win, &ren)) {
        SDL_Log("window: %s", SDL_GetError());
        return 1;
    }
    /* vsync, so scrolling does not tear; the pacing below (60.1 Hz, PAL 50 Hz)
     * still sets the speed. Not on displays slower than the game: waiting for
     * them would slow it down.
     * shortcut: checked once at start, not when the window moves to another display */
    const SDL_DisplayMode *mode = SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(win));
    const double fps = nes_frame_rate(&nes);
    if (!mode || mode->refresh_rate == 0 || mode->refresh_rate >= fps - 1) SDL_SetRenderVSync(ren, 1);
    if (cfg.fullscreen) SDL_SetWindowFullscreen(win, true);
    SDL_Texture *frame_tex = cfg.tv_frame ? make_tv_frame(ren, width, lines) : NULL;
    uint64_t message_until = 0;  /* a short message in the title bar */
    SDL_Texture *tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_XRGB8888,
                                         SDL_TEXTUREACCESS_STREAMING, NES_W, NES_H);
    SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_PIXELART);

    /* Audio: mono float at the APU's 48 kHz. Video sets the pace (60.1 or 50 Hz);
     * the playback rate is nudged (+-0.5%) to keep about 3 frames queued, so
     * the two clocks never drift apart (no crackle, no growing latency). */
    const SDL_AudioSpec spec = {SDL_AUDIO_F32, 1, APU_SAMPLE_RATE};
    SDL_AudioStream *audio = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL, NULL);
    if (audio) SDL_ResumeAudioStreamDevice(audio);
    else SDL_Log("no audio: %s", SDL_GetError());

    static Emu emu;
    emu.nes = &nes;
    emu.lock = SDL_CreateMutex();
    nes_palette_rgb(emu.palette);
    emu.jam_pc = -1;
    emu.audio = audio;
    emu.fps = fps;
    emu.max_frames = max_frames;
    emu.replay = &replay_log;
    emu.record = record_file;
    SDL_Thread *thread = SDL_CreateThread(emu_thread, "emulator", &emu);
    if (!thread) {
        SDL_Log("thread: %s", SDL_GetError());
        return 1;
    }
    static uint32_t fb[NES_W * NES_H];
    int running = 1;
    while (running && !SDL_GetAtomicInt(&emu.done)) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_EVENT_QUIT) running = 0;
            if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE) running = 0;
            if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat) {
                const char *msg = NULL;
                char err[128], text[160];
                if (e.key.key == SDLK_F5) SDL_SetAtomicInt(&emu.reset, 1);
                if (e.key.key == SDLK_F11 || (e.key.key == SDLK_RETURN && (e.key.mod & SDL_KMOD_ALT)))
                    SDL_SetWindowFullscreen(win, !(SDL_GetWindowFlags(win) & SDL_WINDOW_FULLSCREEN));
                if (e.key.key == SDLK_F10) {
                    if (frame_tex) SDL_DestroyTexture(frame_tex), frame_tex = NULL;
                    else frame_tex = make_tv_frame(ren, width, lines);
                }
                SDL_LockMutex(emu.lock);
                if (e.key.key == SDLK_F2) msg = nes_state_save(&nes, state_path) == 0 ? "state saved" : "could not save state";
                if (e.key.key == SDLK_F4) {
                    if (nes_state_load(&nes, state_path, err, sizeof err) == 0) msg = "state loaded";
                    else SDL_snprintf(text, sizeof text, "no state loaded (%s)", err), msg = text;
                }
                if (e.key.key == SDLK_F6 && nes.cart.disk) {  /* FDS: the next side, after half a second out */
                    int cur = nes.cart.disk_side >= 0 ? nes.cart.disk_side : nes.cart.disk_next;
                    int side = (cur + 1) % nes.cart.disk_sides;
                    cart_fds_insert(&nes.cart, side, nes.cpu.cycles + 900000);
                    SDL_snprintf(text, sizeof text, "disk %d side %c", side / 2 + 1, 'A' + side % 2), msg = text;
                }
                SDL_UnlockMutex(emu.lock);
                if (msg) {
                    SDL_snprintf(title, sizeof title, "%s - %s", heading, msg);
                    SDL_SetWindowTitle(win, title);
                    message_until = SDL_GetTicks() + 2000;
                }
            }
            gamepad_event(&e);
        }
        SDL_SetAtomicInt(&emu.pads, input_clean(keyboard_buttons() | gamepad_buttons(gamepads[0])) |
                                        input_clean(gamepad_buttons(gamepads[1])) << 8);

        SDL_LockMutex(emu.lock);
        const int fresh = emu.fb_new, jam_pc = emu.jam_pc;
        if (fresh) SDL_memcpy(fb, emu.fb, sizeof fb), emu.fb_new = 0;
        SDL_UnlockMutex(emu.lock);
        if (!fresh) {
            SDL_Delay(1);
            continue;
        }
        char jam_line[32];
        SDL_snprintf(jam_line, sizeof jam_line, "CPU jammed at $%04X", jam_pc);
        const char *jam_banner[] = {jam_line, "F5 reset, F4 load state", forced ? "(region forced in nes.ini)" : NULL, NULL};
        SDL_UpdateTexture(tex, NULL, fb, NES_W * sizeof fb[0]);
        present(ren, tex, frame_tex, cfg.scale, width, cols, lines, jam_pc >= 0 ? jam_banner : NULL);
        if (message_until && SDL_GetTicks() > message_until) {
            SDL_SetWindowTitle(win, heading);
            message_until = 0;
        }
    }
    SDL_SetAtomicInt(&emu.quit, 1);
    SDL_WaitThread(thread, NULL);
    SDL_DestroyMutex(emu.lock);
    if (record_file) fclose(record_file);
    input_log_free(&replay_log);
    for (int i = 0; i < 2; i++)
        if (gamepads[i]) SDL_CloseGamepad(gamepads[i]);
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
    if (audio) SDL_DestroyAudioStream(audio);
    SDL_DestroyTexture(tex);
    if (frame_tex) SDL_DestroyTexture(frame_tex);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
