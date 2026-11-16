/* Procedural TV frame (see tv_frame.h). Shapes are signed distance fields
 * of rounded rectangles, evaluated per texture pixel; edges are
 * anti-aliased over one texture pixel. Distances are in NES pixels. */
#include "tv_frame.h"

#include <math.h>
#include <stddef.h>

static float clampf(float x, float lo, float hi) { return x < lo ? lo : x > hi ? hi : x; }

static float smooth(float e0, float e1, float x) {
    float t = clampf((x - e0) / (e1 - e0), 0, 1);
    return t * t * (3 - 2 * t);
}

/* Signed distance from (x, y) to a rounded rectangle (negative inside). */
static float rounded_rect(float x, float y, float cx, float cy, float hw, float hh, float r) {
    float qx = fabsf(x - cx) - (hw - r), qy = fabsf(y - cy) - (hh - r);
    float ox = qx > 0 ? qx : 0, oy = qy > 0 ? qy : 0;
    return sqrtf(ox * ox + oy * oy) + fminf(fmaxf(qx, qy), 0) - r;
}

static float noise(int x, int y) {  /* fixed grain for the plastic, -1..1 */
    uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return (float)(h & 0xFFFF) / 32767.5f - 1;
}

void tv_frame_render(uint8_t *rgba) {
    const int W = TV_W * TV_RES, H = TV_H * TV_RES;
    const float aa = 1.0f / TV_RES;  /* one texture pixel, in NES pixels */
    const float sx = TV_LEFT + 128, sy = TV_TOP + 120;  /* screen centre */
    const float led_x = TV_W - 34, led_y = TV_H - TV_BOTTOM / 2.0f;
    for (int py = 0; py < H; py++) {
        for (int px = 0; px < W; px++) {
            float x = (px + 0.5f) * aa, y = (py + 0.5f) * aa;
            float d_body = rounded_rect(x, y, TV_W / 2.0f, TV_H / 2.0f, TV_W / 2.0f, TV_H / 2.0f, 16);
            float d_scr = rounded_rect(x, y, sx, sy, 128, 120, 10);
            float r, g, b, a;
            if (d_scr < 0) {
                /* screen: transparent, darkened towards the edges (tube
                 * curvature, inner shadow) with a faint glass reflection */
                float ex = (x - sx) / 128, ey = (y - sy) / 120;
                float dark = 0.55f * (1 - smooth(0, 7, -d_scr)) + 0.25f * smooth(0.55f, 1.25f, sqrtf(ex * ex + ey * ey));
                float gx = (x - (TV_LEFT + 70)) / 120, gy = (y - (TV_TOP + 45)) / 70;
                float glare = 0.07f * (1 - smooth(0.2f, 1, sqrtf(gx * gx + gy * gy)));
                dark = clampf(dark, 0, 1);
                a = 1 - (1 - dark) * (1 - glare);
                r = g = b = a > 0 ? 255 * glare / a : 0;
            } else {
                /* bezel plastic: vertical gradient and grain */
                float t = y / TV_H;
                float base = 62 - 30 * t + 1.6f * noise(px, py);
                r = base, g = base * 0.97f, b = base * 0.94f;
                /* rounded outer edge: lit from above */
                float rim = 1 - smooth(0, 3, -d_body);
                float lit = 34 - 52 * smooth(0.25f, 0.75f, t);
                r += rim * lit, g += rim * lit, b += rim * lit;
                /* recess around the screen: near-black, its lower lip catching light */
                if (d_scr < 6) {
                    float k = smooth(0, 6, d_scr);
                    float lip = 14 * smooth(sy - 60, sy + 60, y) * (1 - k);
                    r = r * k + (10 + lip) * (1 - k);
                    g = g * k + (10 + lip) * (1 - k);
                    b = b * k + (12 + lip) * (1 - k);
                }
                /* bevel where the recess meets the bezel */
                float bevel = 1 - clampf(fabsf(d_scr - 6.5f) / 1.2f, 0, 1);
                float shade = -12 + 28 * smooth(sy - 90, sy + 90, y);
                r += bevel * shade, g += bevel * shade, b += bevel * shade;
                /* speaker grille: slots in the chin */
                if (x > 26 && x < 70 && y > TV_H - 28 && y < TV_H - 8) {
                    float slot = fmodf(y - (TV_H - 28), 4);
                    if (slot > 1.2f && slot < 2.6f) r *= 0.45f, g *= 0.45f, b *= 0.45f;
                }
                /* power LED with a small glow */
                float dl = sqrtf((x - led_x) * (x - led_x) + (y - led_y) * (y - led_y));
                float led = 1 - smooth(1.6f, 1.6f + aa, dl), glow = 0.45f * (1 - smooth(1.6f, 6, dl));
                r = r + (255 - r) * led + 200 * glow * (1 - led);
                g = g * (1 - led) + 40 * led + 20 * glow * (1 - led);
                b = b * (1 - led) + 30 * led;
                a = 1 - smooth(-aa, 0, d_body);  /* anti-aliased outline; outside is transparent */
            }
            uint8_t *o = rgba + ((size_t)py * W + px) * 4;
            o[0] = (uint8_t)clampf(r, 0, 255);
            o[1] = (uint8_t)clampf(g, 0, 255);
            o[2] = (uint8_t)clampf(b, 0, 255);
            o[3] = (uint8_t)clampf(a * 255 + 0.5f, 0, 255);
        }
    }
}
