/* SDL3 frontend: runs a ROM in the interpreter (M3/M4).
 * Usage: nes ROM [--frames N]   (N > 0: quit after N frames, for smoke tests)
 * Keys: arrows = D-pad, X = A, Z = B, Enter = Start, Right Shift = Select,
 *       F5 = reset, Esc = quit. Gamepads: M4. */
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <stdlib.h>
#include <string.h>

#include "nes.h"

enum { NES_W = 256, NES_H = 240, SCALE = 3 };
static const double FRAME_NS = 1e9 / 60.0988;  /* NTSC frame rate, docs/04 */

static uint8_t keyboard_pad(void) {
    const bool *k = SDL_GetKeyboardState(NULL);
    uint8_t b = 0;
    if (k[SDL_SCANCODE_X]) b |= PAD_A;
    if (k[SDL_SCANCODE_Z]) b |= PAD_B;
    if (k[SDL_SCANCODE_RSHIFT]) b |= PAD_SELECT;
    if (k[SDL_SCANCODE_RETURN]) b |= PAD_START;
    if (k[SDL_SCANCODE_UP]) b |= PAD_UP;
    if (k[SDL_SCANCODE_DOWN]) b |= PAD_DOWN;
    if (k[SDL_SCANCODE_LEFT]) b |= PAD_LEFT;
    if (k[SDL_SCANCODE_RIGHT]) b |= PAD_RIGHT;
    return b;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        SDL_Log("usage: nes ROM [--frames N]");
        return 2;
    }
    long max_frames = 0;
    for (int i = 2; i + 1 < argc; i++)
        if (!strcmp(argv[i], "--frames")) max_frames = atol(argv[i + 1]);

    size_t size;
    void *rom = SDL_LoadFile(argv[1], &size);
    if (!rom) {
        SDL_Log("%s: %s", argv[1], SDL_GetError());
        return 1;
    }
    static Nes nes;
    char err[128];
    if (nes_load(&nes, rom, size, err, sizeof err) != 0) {
        SDL_Log("%s: %s", argv[1], err);
        return 1;
    }
    SDL_free(rom);

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("SDL_Init: %s", SDL_GetError());
        return 1;
    }
    SDL_Window *win;
    SDL_Renderer *ren;
    if (!SDL_CreateWindowAndRenderer("nes-recomp", NES_W * SCALE, NES_H * SCALE,
                                     SDL_WINDOW_RESIZABLE, &win, &ren)) {
        SDL_Log("window: %s", SDL_GetError());
        return 1;
    }
    SDL_SetRenderLogicalPresentation(ren, NES_W, NES_H, SDL_LOGICAL_PRESENTATION_INTEGER_SCALE);
    SDL_Texture *tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_XRGB8888,
                                         SDL_TEXTUREACCESS_STREAMING, NES_W, NES_H);
    SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_NEAREST);

    uint32_t palette[64];
    nes_palette_rgb(palette);
    static uint32_t fb[NES_W * NES_H];
    uint64_t next = SDL_GetTicksNS();
    int running = 1;
    for (long frame = 0; running && (max_frames <= 0 || frame < max_frames); frame++) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_EVENT_QUIT) running = 0;
            if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE) running = 0;
            if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_F5) nes_reset(&nes);
        }
        nes.pad[0] = keyboard_pad();
        nes_run_frame(&nes);
        if (nes.cpu.jammed) {
            SDL_Log("CPU jammed at $%04X", nes.cpu.pc);
            break;
        }
        for (int i = 0; i < NES_W * NES_H; i++) fb[i] = palette[nes.ppu.fb[i] & 0x3F];
        SDL_UpdateTexture(tex, NULL, fb, NES_W * sizeof fb[0]);
        SDL_RenderClear(ren);
        SDL_RenderTexture(ren, tex, NULL, NULL);
        SDL_RenderPresent(ren);

        next += (uint64_t)FRAME_NS;  /* pace to 60.1 Hz regardless of monitor rate */
        uint64_t now = SDL_GetTicksNS();
        if (next > now) SDL_DelayPrecise(next - now);
        else next = now;  /* fell behind: don't try to catch up */
    }
    cart_free(&nes.cart);
    SDL_DestroyTexture(tex);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
