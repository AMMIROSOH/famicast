/* A TV-like frame drawn around the picture: dark plastic bezel, recessed
 * screen with rounded corners, inner shadow, glass reflection, power LED.
 * Generated procedurally (no image files), as an RGBA overlay drawn on top
 * of the picture: transparent where the picture shows through. */
#ifndef NES_TV_FRAME_H
#define NES_TV_FRAME_H

#include <stdint.h>

/* Layout in NES pixels: the picture (256x240) sits at (TV_LEFT, TV_TOP). */
enum {
    TV_LEFT = 24, TV_TOP = 20, TV_BOTTOM = 36,
    TV_W = 256 + 2 * TV_LEFT, TV_H = 240 + TV_TOP + TV_BOTTOM,
    TV_RES = 4  /* texture pixels per NES pixel, so the frame stays smooth when scaled up */
};

/* rgba: (TV_W * TV_RES) x (TV_H * TV_RES) pixels, bytes R, G, B, A. */
void tv_frame_render(uint8_t *rgba);

#endif
