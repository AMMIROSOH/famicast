/* A TV-like frame drawn around the picture: dark plastic bezel, recessed
 * screen with rounded corners, inner shadow, glass reflection, power LED.
 * Generated procedurally (no image files), as an RGBA overlay drawn on top
 * of the picture: transparent where the picture shows through. */
#ifndef NES_TV_FRAME_H
#define NES_TV_FRAME_H

#include <stdint.h>

/* Layout in NES lines (square units): the picture, `width` wide (256, or
 * 293 with TV-shaped 8:7 pixels) and `lines` high (240, or 224 with the
 * overscan hidden), sits at (TV_LEFT, TV_TOP). */
enum {
    TV_LEFT = 24, TV_TOP = 20, TV_BOTTOM = 36,
    TV_RES = 4  /* texture pixels per unit, so the frame stays smooth when scaled up */
};
static inline int tv_w(int width) { return width + 2 * TV_LEFT; }
static inline int tv_h(int lines) { return lines + TV_TOP + TV_BOTTOM; }

/* rgba: (tv_w(width) * TV_RES) x (tv_h(lines) * TV_RES) pixels, bytes R, G, B, A. */
void tv_frame_render(uint8_t *rgba, int width, int lines);

#endif
