/* NES palette from an NTSC signal model, so no third-party palette table is
 * copied. Signal levels and method: nesdev wiki "NTSC video" (Bisqwit's
 * model). The 3.9 hue offset puts $16 at red, $12 at blue, $1A at green;
 * checked against the usual palette layout (systems/nes/docs/verification-log.md).
 * Colour emphasis ($2001 bits 5-7, NTSC: red, green, blue): the PPU
 * attenuates its output while the colour wave of phase 0, 4 or 8 is high
 * (the hue opposite red, green or blue), by 0.816328 of the absolute
 * voltage, for every colour but columns $xE/$xF ("NTSC video", "Colour
 * emphasis"). Entry = emphasis << 6 | colour. */
#include <math.h>

#include "ppu.h"

void nes_palette_rgb(uint32_t out[512]) {
    static const float black = .518f, white = 1.962f;
    static const float levels[8] = {.350f, .518f, .962f, 1.550f,    /* signal low */
                                    1.094f, 1.506f, 1.962f, 1.962f}; /* signal high */
    const float pi = 3.14159265f;
    for (int px = 0; px < 512; px++) {
        int color = px & 15, level = (px >> 4) & 3, emph = px >> 6;
        if (color > 13) level = 1;
        float lo = levels[level], hi = levels[4 + level];
        if (color == 0) lo = hi;
        if (color > 12) hi = lo;
        float y = 0, i = 0, q = 0;
        for (int p = 0; p < 12; p++) {
            float s = ((color + p) % 12 < 6) ? hi : lo;
            if (color < 14 && (((emph & 1) && p % 12 < 6) || ((emph & 2) && (4 + p) % 12 < 6) ||
                               ((emph & 4) && (8 + p) % 12 < 6)))
                s *= 0.816328f;
            s = (s - black) / (white - black);
            y += s;
            i += s * cosf(pi * (p + 3.9f) / 6);
            q += s * sinf(pi * (p + 3.9f) / 6);
        }
        y /= 12; i /= 12; q /= 12;
        float rgb[3] = {y + 0.946882f * i + 0.623557f * q,
                        y - 0.274788f * i - 0.635691f * q,
                        y - 1.108545f * i + 1.709007f * q};
        uint32_t c = 0;
        for (int k = 0; k < 3; k++) {
            float v = rgb[k] < 0 ? 0 : rgb[k] > 1 ? 1 : rgb[k];
            c = c << 8 | (uint32_t)(v * 255 + 0.5f);
        }
        out[px] = c;
    }
}
