/* SDL3 frontend. M0: window + 256x240 framebuffer texture.
 * Usage: nes [--frames N]   (N > 0: quit after N frames, for smoke tests) */
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <stdlib.h>
#include <string.h>

enum { NES_W = 256, NES_H = 240, SCALE = 3 };

int main(int argc, char **argv) {
    long max_frames = 0;
    for (int i = 1; i + 1 < argc; i++)
        if (!strcmp(argv[i], "--frames")) max_frames = atol(argv[i + 1]);

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
    SDL_SetRenderVSync(ren, 1);
    SDL_SetRenderLogicalPresentation(ren, NES_W, NES_H, SDL_LOGICAL_PRESENTATION_INTEGER_SCALE);
    SDL_Texture *tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_XRGB8888,
                                         SDL_TEXTUREACCESS_STREAMING, NES_W, NES_H);
    SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_NEAREST);

    static uint32_t fb[NES_W * NES_H];
    int running = 1;
    for (long frame = 0; running && (max_frames <= 0 || frame < max_frames); frame++) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_EVENT_QUIT) running = 0;
            if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE) running = 0;
        }
        /* placeholder until the PPU exists (M3): moving test pattern */
        for (int y = 0; y < NES_H; y++)
            for (int x = 0; x < NES_W; x++)
                fb[y * NES_W + x] = (uint32_t)(((x + frame) & 0xFF) << 16 | (y & 0xFF) << 8 | 0x40);
        SDL_UpdateTexture(tex, NULL, fb, NES_W * sizeof fb[0]);
        SDL_RenderClear(ren);
        SDL_RenderTexture(ren, tex, NULL, NULL);
        SDL_RenderPresent(ren);
    }
    SDL_DestroyTexture(tex);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
