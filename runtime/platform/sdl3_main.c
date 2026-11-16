/* SDL3 frontend: runs a ROM (recompiled code where linked, else interpreted).
 * Usage: nes ROM [--frames N] [--record FILE] [--replay FILE]
 *   ROM may be left out in recompiled builds: the ROM they embed is used.
 *   --frames N   quit after N frames (smoke tests)
 *   --record     save the input of every frame (runtime/input_log.h)
 *   --replay     play a recording back; live input resumes when it ends
 * Keyboard (player 1): arrows, X = A, Z = B, Enter = Start, Right Shift =
 * Select, F5 = reset, Esc = quit. F2 = save state, F4 = load state,
 * F10 = TV frame on/off, F11 or Alt+Enter = fullscreen on/off.
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
static const double FRAME_NS = 1e9 / 60.0988;  /* NTSC frame rate, docs/04 */

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

static SDL_Texture *make_tv_frame(SDL_Renderer *ren, int lines) {
    const int w = TV_W * TV_RES, h = tv_h(lines) * TV_RES;
    uint8_t *px = SDL_malloc((size_t)w * h * 4);
    tv_frame_render(px, lines);
    SDL_Texture *t = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, w, h);
    if (t) {
        SDL_UpdateTexture(t, NULL, px, w * 4);
        SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(t, SDL_SCALEMODE_LINEAR);
    }
    SDL_free(px);
    return t;
}

/* Draw the picture's visible lines (240, or 224 with the overscan hidden:
 * centred) and the frame over it, scaled into the output. */
static void present(SDL_Renderer *ren, SDL_Texture *pic, SDL_Texture *frame, int scale, int lines) {
    int W, H;
    SDL_GetCurrentRenderOutputSize(ren, &W, &H);
    float cw = frame ? TV_W : NES_W, ch = (float)(frame ? tv_h(lines) : lines);  /* content, in NES pixels */
    float sx = W / cw, sy = H / ch;
    if (scale != SCALE_STRETCH) {
        sx = sy = SDL_min(sx, sy);
        if (scale == SCALE_INTEGER && sx >= 1) sx = sy = SDL_floorf(sx);
    }
    SDL_FRect content = {SDL_floorf((W - cw * sx) / 2), SDL_floorf((H - ch * sy) / 2), cw * sx, ch * sy};
    SDL_FRect picture = {content.x + (frame ? TV_LEFT * sx : 0), content.y + (frame ? TV_TOP * sy : 0),
                         NES_W * sx, lines * sy};
    SDL_FRect src = {0, (NES_H - lines) / 2.0f, NES_W, (float)lines};
    SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
    SDL_RenderClear(ren);
    SDL_RenderTexture(ren, pic, &src, &picture);
    if (frame) SDL_RenderTexture(ren, frame, NULL, &content);
    SDL_RenderPresent(ren);
}

int main(int argc, char **argv) {
    const char *rom_path = argc > 1 && argv[1][0] != '-' ? argv[1] : NULL;
    const int embedded = !rom_path && recomp_program && recomp_program->rom;
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
    static Nes nes;
    char err[128];
    if (nes_load(&nes, embedded ? recomp_program->rom : rom, size, err, sizeof err) != 0) {
        SDL_Log("%s: %s", embedded ? recomp_program->rom_name : rom_path, err);
        return 1;
    }
    SDL_free(rom);
    /* battery-backed PRG RAM (saves): ROM path + ".sav", 8 KB; for the
     * embedded ROM, its file name + ".sav" next to the program */
    char sav_path[1024], state_path[1024], ini_path[1024], title[256];
    const char *name = embedded ? recomp_program->rom_name : rom_path;
    if (embedded) SDL_snprintf(sav_path, sizeof sav_path, "%s%s", SDL_GetBasePath(), recomp_program->rom_name);
    else SDL_snprintf(sav_path, sizeof sav_path, "%s", rom_path);
    SDL_snprintf(state_path, sizeof state_path, "%s.state", sav_path);
    SDL_strlcat(sav_path, ".sav", sizeof sav_path);
    for (const char *p = name; *p; p++)
        if (*p == '/' || *p == '\\') name = p + 1;
    SDL_snprintf(ini_path, sizeof ini_path, "%s%s.ini", SDL_GetBasePath(), NES_APP_NAME);
    Settings cfg;
    settings_load(&cfg, ini_path);
    char misses_path[1024];
    SDL_snprintf(misses_path, sizeof misses_path, "%s%s.misses", SDL_GetBasePath(), NES_APP_NAME);
    if (nes.recomp && cfg.record_misses) nes.misses = SDL_calloc(nes_miss_slots(&nes), sizeof *nes.misses);
    if (nes.cart.battery) {
        size_t sav_size;
        void *sav = SDL_LoadFile(sav_path, &sav_size);
        if (sav && sav_size == nes.cart.prg_ram_size) SDL_memcpy(nes.cart.prg_ram, sav, sav_size);
        SDL_free(sav);
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
    int k = cfg.window_scale, cw = cfg.tv_frame ? TV_W : NES_W, ch = cfg.tv_frame ? tv_h(lines) : lines;
    SDL_Rect usable;
    if (SDL_GetDisplayUsableBounds(SDL_GetPrimaryDisplay(), &usable))
        while (k > 1 && (cw * k > usable.w || ch * k > usable.h - 40)) k--;
    SDL_snprintf(title, sizeof title, "%s", name);
    if (!SDL_CreateWindowAndRenderer(title, cw * k, ch * k, SDL_WINDOW_RESIZABLE, &win, &ren)) {
        SDL_Log("window: %s", SDL_GetError());
        return 1;
    }
    /* vsync, so scrolling does not tear; the 60.1 Hz pacing below still sets the
     * speed. Not on displays under 59 Hz: waiting for them would slow the game.
     * shortcut: checked once at start, not when the window moves to another display */
    const SDL_DisplayMode *mode = SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(win));
    if (!mode || mode->refresh_rate == 0 || mode->refresh_rate >= 59) SDL_SetRenderVSync(ren, 1);
    if (cfg.fullscreen) SDL_SetWindowFullscreen(win, true);
    SDL_Texture *frame_tex = cfg.tv_frame ? make_tv_frame(ren, lines) : NULL;
    uint64_t message_until = 0;  /* a short message in the title bar */
    SDL_Texture *tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_XRGB8888,
                                         SDL_TEXTUREACCESS_STREAMING, NES_W, NES_H);
    SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_NEAREST);

    uint32_t palette[64];
    nes_palette_rgb(palette);
    static uint32_t fb[NES_W * NES_H];
    /* Audio: mono float at the APU's 48 kHz. Video sets the pace (60.1 Hz);
     * the playback rate is nudged (+-0.5%) to keep about 3 frames queued, so
     * the two clocks never drift apart (no crackle, no growing latency). */
    const SDL_AudioSpec spec = {SDL_AUDIO_F32, 1, APU_SAMPLE_RATE};
    SDL_AudioStream *audio = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL, NULL);
    if (audio) SDL_ResumeAudioStreamDevice(audio);
    else SDL_Log("no audio: %s", SDL_GetError());
    static float samples[APU_BUF];
    uint64_t next = SDL_GetTicksNS();
    int running = 1, reset_pressed = 0;
    for (long frame = 0; running && (max_frames <= 0 || frame < max_frames); frame++) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_EVENT_QUIT) running = 0;
            if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE) running = 0;
            if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat) {
                const char *msg = NULL;
                char err[128], text[160];
                if (e.key.key == SDLK_F5) reset_pressed = 1;
                if (e.key.key == SDLK_F11 || (e.key.key == SDLK_RETURN && (e.key.mod & SDL_KMOD_ALT)))
                    SDL_SetWindowFullscreen(win, !(SDL_GetWindowFlags(win) & SDL_WINDOW_FULLSCREEN));
                if (e.key.key == SDLK_F10) {
                    if (frame_tex) SDL_DestroyTexture(frame_tex), frame_tex = NULL;
                    else frame_tex = make_tv_frame(ren, lines);
                }
                if (e.key.key == SDLK_F2) msg = nes_state_save(&nes, state_path) == 0 ? "state saved" : "could not save state";
                if (e.key.key == SDLK_F4) {
                    if (nes_state_load(&nes, state_path, err, sizeof err) == 0) msg = "state loaded";
                    else SDL_snprintf(text, sizeof text, "no state loaded (%s)", err), msg = text;
                }
                if (msg) {
                    SDL_snprintf(title, sizeof title, "%s - %s", name, msg);
                    SDL_SetWindowTitle(win, title);
                    message_until = SDL_GetTicks() + 2000;
                }
            }
            gamepad_event(&e);
        }
        InputFrame in;
        if (frame < replay_log.count) {
            in = replay_log.frames[frame];
        } else {
            in.pad[0] = input_clean(keyboard_buttons() | gamepad_buttons(gamepads[0]));
            in.pad[1] = input_clean(gamepad_buttons(gamepads[1]));
            in.flags = reset_pressed ? INPUT_RESET : 0;
        }
        reset_pressed = 0;
        if (record_file) input_log_write(record_file, in);
        if (in.flags & INPUT_RESET) nes_reset(&nes);
        nes.pad[0] = in.pad[0];
        nes.pad[1] = in.pad[1];

        nes_run_frame(&nes);
        int ns = apu_take_samples(&nes.apu, samples, APU_BUF);
        if (audio) {
            SDL_PutAudioStreamData(audio, samples, ns * (int)sizeof(float));
            const float target = 3.0f * APU_SAMPLE_RATE / 60.0988f;
            float queued = (float)SDL_GetAudioStreamQueued(audio) / sizeof(float);
            float ratio = 1.0f + 0.005f * (queued - target) / target;
            SDL_SetAudioStreamFrequencyRatio(audio, ratio < 0.995f ? 0.995f : ratio > 1.005f ? 1.005f : ratio);
        }
        if (nes.cpu.jammed) {
            SDL_Log("CPU jammed at $%04X", nes.cpu.pc);
            break;
        }
        for (int i = 0; i < NES_W * NES_H; i++) fb[i] = palette[nes.ppu.fb[i] & 0x3F];
        SDL_UpdateTexture(tex, NULL, fb, NES_W * sizeof fb[0]);
        present(ren, tex, frame_tex, cfg.scale, lines);
        if (message_until && SDL_GetTicks() > message_until) {
            SDL_SetWindowTitle(win, name);
            message_until = 0;
        }

        next += (uint64_t)FRAME_NS;  /* pace to 60.1 Hz regardless of monitor rate */
        uint64_t now = SDL_GetTicksNS();
        if (next > now) SDL_DelayPrecise(next - now);
        else next = now;  /* fell behind: don't try to catch up */
    }
    if (record_file) fclose(record_file);
    input_log_free(&replay_log);
    for (int i = 0; i < 2; i++)
        if (gamepads[i]) SDL_CloseGamepad(gamepads[i]);
    if (nes.cart.battery && !SDL_SaveFile(sav_path, nes.cart.prg_ram, nes.cart.prg_ram_size))
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
