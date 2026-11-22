/* The VDP (vdp.h). Sources: Charles MacDonald's genvdp.txt, plutiedev.com,
 * the Sega Retro wiki and Genesis Plus GX's behaviour, from memory
 * (docs/sources.md); checked by systems/genesis/tests/md_tests.c and the contact
 * sheet of the licensed set, not yet by public test ROMs. */
#include <string.h>

#include "md.h"

#define R(n) (m->vdp.reg[n])

static int h40(const Md *m) { return R(12) & 1; }
int vdp_height(const Md *m) { return m->pal && (R(1) & 8) ? 240 : 224; }
static int display_on(const Md *m) { return R(1) & 0x40; }

static uint16_t vram16(const Vdp *d, uint32_t a) {
    a &= 0xFFFE;
    return (uint16_t)(d->vram[a] << 8 | d->vram[a + 1]);
}

/* ---- interrupts: VINT level 6, HINT level 4 ---- */
static void update_ipl(Md *m) {
    Vdp *d = &m->vdp;
    m->cpu.ipl = (uint8_t)(d->vint && (R(1) & 0x20) ? 6 : d->hint && (R(0) & 0x10) ? 4 : 0);
}
void vdp_iack(Md *m, int level) {
    if (level == 6) m->vdp.vint = 0;
    else if (level == 4) m->vdp.hint = 0;
    update_ipl(m);
}

/* ---- the timeline: line and position in it ---- */
static int line_now(const Md *m) { return (int)((md_now(m) - m->frame_start) / MD_LINE_CLOCKS); }
/* H counter: the line split evenly over its 210 (H40) or 171 (H32) counts,
 * with the jump in the blank. shortcut: real counts are not evenly spaced (H40 mixes two pixel clocks) */
static int h_now(const Md *m) {
    unsigned p = (unsigned)((md_now(m) - m->frame_start) % MD_LINE_CLOCKS);
    if (h40(m)) {
        unsigned i = p * 210 / MD_LINE_CLOCKS;
        return (int)(i < 0xB7 ? i : i - 0xB7 + 0xE4);
    }
    unsigned i = p * 171 / MD_LINE_CLOCKS;
    return (int)(i < 0x94 ? i : i - 0x94 + 0xE9);
}
static int v_count(const Md *m, int line) {
    if (m->pal) return (line <= (vdp_height(m) == 240 ? 0x10A : 0x102) ? line : line + 0xC7) & 0xFF;
    return (line <= 0xEA ? line : line - 6) & 0xFF;  /* shortcut: interlace counts are not modelled */
}

/* ---- the data port ---- */
static void store(Md *m, uint16_t v) {
    Vdp *d = &m->vdp;
    unsigned i = d->addr >> 1 & 0x3F;
    switch (d->code & 0xF) {
    case 1:  /* VRAM: an odd address swaps the bytes */
        if (d->addr & 1) v = (uint16_t)(v >> 8 | v << 8);
        d->vram[d->addr & 0xFFFE] = (uint8_t)(v >> 8);
        d->vram[(d->addr & 0xFFFE) + 1] = (uint8_t)v;
        break;
    case 3: d->cram[i] = v & 0x0EEE; break;
    case 5: if (i < 40) d->vsram[i] = v & 0x07FF; break;
    }
    d->addr = (uint16_t)(d->addr + R(15));
}

static unsigned dma_length(const Md *m) { unsigned n = R(19) | R(20) << 8; return n ? n : 0x10000; }
/* Words (68000 to VDP) or bytes (fill) a line moves: in the picture, or in blank or with the display off. */
static unsigned dma_rate(const Md *m) {
    int blank = line_now(m) >= vdp_height(m) || !display_on(m);
    return h40(m) ? (blank ? 205 : 18) : (blank ? 167 : 16);
}

static void fill(Md *m, uint16_t v) {
    Vdp *d = &m->vdp;
    unsigned n = dma_length(m);
    for (unsigned k = 0; k < n; k++) {
        if ((d->code & 0xF) == 1) d->vram[d->addr ^ 1] = (uint8_t)(v >> 8);  /* the data's upper byte, to the other half */
        else store(m, v), d->addr = (uint16_t)(d->addr - R(15));  /* CRAM, VSRAM: the whole word */
        d->addr = (uint16_t)(d->addr + R(15));
    }
    R(19) = R(20) = 0;
    d->dma_end = md_now(m) + (uint64_t)n * MD_LINE_CLOCKS / dma_rate(m);
    d->fill = 0;
}

static uint16_t data_read(Md *m) {
    Vdp *d = &m->vdp;
    unsigned i = d->addr >> 1 & 0x3F;
    uint16_t v;
    switch (d->code & 0xF) {
    case 0: v = vram16(d, d->addr); break;
    case 4: v = i < 40 ? d->vsram[i] : d->vsram[0]; break;
    case 8: v = d->cram[i]; break;
    case 0xC: v = d->vram[d->addr ^ 1]; break;  /* 8-bit VRAM read (undocumented); shortcut: the upper byte is open bus, read as 0 */
    default: v = 0;
    }
    d->addr = (uint16_t)(d->addr + R(15));
    return v;
}

/* ---- DMA ---- */
static void dma_68k(Md *m) {
    uint32_t src = (uint32_t)(R(23) & 0x7F) << 17 | R(22) << 9 | R(21) << 1;
    unsigned n = dma_length(m);
    for (unsigned k = 0; k < n; k++) {
        store(m, md_read16(m, src));
        src = (src & 0xFE0000) | ((src + 2) & 0x1FFFF);  /* the source wraps within 128 KB */
    }
    R(19) = R(20) = 0;
    R(21) = (uint8_t)(src >> 1), R(22) = (uint8_t)(src >> 9);
    /* the 68000 waits while the VDP takes its bus. shortcut: at the rate of the line the DMA starts on */
    m->cpu.cycles += (uint64_t)n * MD_LINE_CLOCKS / dma_rate(m) / 7;
}

static void dma_copy(Md *m) {
    Vdp *d = &m->vdp;
    unsigned n = dma_length(m), src = R(21) | R(22) << 8;
    for (unsigned k = 0; k < n; k++, src++) {
        d->vram[d->addr] = d->vram[src & 0xFFFF];
        d->addr = (uint16_t)(d->addr + R(15));
    }
    R(19) = R(20) = 0;
    R(21) = (uint8_t)src, R(22) = (uint8_t)(src >> 8);
    d->dma_end = md_now(m) + (uint64_t)n * 2 * MD_LINE_CLOCKS / dma_rate(m);  /* half a fill's rate */
}

/* ---- the ports ---- */
uint16_t vdp_port_read(Md *m, uint32_t a) {
    Vdp *d = &m->vdp;
    a &= 0x1E;
    if (a < 4) {
        d->pending = 0;
        return data_read(m);
    }
    int line = line_now(m), h = h_now(m);
    if (a < 8) {  /* status; the upper 6 bits are open bus */
        int hblank = h40(m) ? h >= 0xB3 || h < 0x06 : h >= 0x93 || h < 0x05;
        uint16_t s = (uint16_t)((m->cpu.ir & 0xFC00) | 0x0200 | d->vint << 7 | d->spr_status | d->odd << 4 |
                                (line >= vdp_height(m) || !display_on(m)) << 3 | hblank << 2 |
                                (md_now(m) < d->dma_end) << 1 | m->pal);
        d->pending = 0;
        d->spr_status = 0;
        return s;
    }
    /* shortcut: the latch of register 0 bit 1 is not modelled */
    return (uint16_t)(v_count(m, line) << 8 | h);
}

void vdp_port_write(Md *m, uint32_t a, uint16_t v) {
    Vdp *d = &m->vdp;
    a &= 0x1E;
    if (a < 4) {
        d->pending = 0;
        store(m, v);
        if (d->fill) fill(m, v);
        return;
    }
    if (a >= 8) return;
    if (d->pending) {  /* the second word: address bits 15-14, code bits 5-2 */
        d->pending = 0;
        d->addr = (uint16_t)((d->addr & 0x3FFF) | (v & 3) << 14);
        d->code = (uint8_t)((d->code & 3) | (v >> 2 & 0x3C));
        if ((d->code & 0x20) && (R(1) & 0x10)) {
            switch (R(23) >> 6) {
            case 2: d->fill = 1; break;
            case 3: dma_copy(m); break;
            default: dma_68k(m);
            }
        }
    } else if ((v & 0xC000) == 0x8000) {  /* a register */
        unsigned r = v >> 8 & 0x1F;
        if (r < 24) R(r) = (uint8_t)v;
        update_ipl(m);
    } else {  /* the first word: address bits 13-0, code bits 1-0 */
        d->pending = 1;
        d->addr = (uint16_t)((d->addr & 0xC000) | (v & 0x3FFF));
        d->code = (uint8_t)((d->code & 0x3C) | v >> 14);
    }
}

/* ---- drawing ---- */
/* A pixel as the layers give it: bit 7 priority, bits 5-4 palette, 3-0 colour (0: transparent). */
static uint8_t tile_px(const Vdp *d, uint16_t e, int x, int y) {
    if (e & 0x0800) x ^= 7;
    if (e & 0x1000) y ^= 7;
    uint8_t b = d->vram[((e & 0x7FF) * 32 + y * 4 + (x >> 1)) & 0xFFFF];
    return (uint8_t)((e >> 8 & 0x80) | (e >> 9 & 0x30) | (x & 1 ? b & 0xF : b >> 4));
}

static int cells(int code) { return code == 3 ? 128 : code == 1 ? 64 : 32; }  /* 2 is not valid: 32 */

static void plane(const Md *m, int line, int b, uint8_t *out, int w) {
    const Vdp *d = &m->vdp;
    int pw = cells(R(16) & 3), ph = cells(R(16) >> 4 & 3);
    uint32_t nt = b ? (uint32_t)(R(4) & 7) << 13 : (uint32_t)(R(2) & 0x38) << 10;
    int hl = (R(11) & 3) == 0 ? 0 : (R(11) & 3) == 2 ? line & ~7 : (R(11) & 3) == 3 ? line : line & 7;
    int hs = vram16(d, ((uint32_t)(R(13) & 0x3F) << 10) + (uint32_t)hl * 4 + (uint32_t)b * 2) & 0x3FF;
    for (int x = 0; x < w; x++) {
        /* per 2-cell column of the screen. shortcut: the partly shown column of a fine scroll is not special */
        int vs = d->vsram[R(11) & 4 ? (x >> 4) * 2 + b : b] & 0x3FF;
        int px = (x - hs) & (pw * 8 - 1), py = (line + vs) & (ph * 8 - 1);
        out[x] = tile_px(d, vram16(d, nt + (uint32_t)((py >> 3) * pw + (px >> 3)) * 2), px & 7, py & 7);
    }
}

static void window(const Md *m, int line, uint8_t *a, int w) {
    const Vdp *d = &m->vdp;
    int wv = (R(18) & 0x1F) * 8, wh = (R(17) & 0x1F) * 16;
    int all = R(18) & 0x80 ? line >= wv : line < wv;
    uint32_t nt = h40(m) ? (uint32_t)(R(3) & 0x3C) << 10 : (uint32_t)(R(3) & 0x3E) << 10;
    int ww = h40(m) ? 64 : 32;
    for (int x = 0; x < w; x++)
        if (all || (R(17) & 0x80 ? x >= wh : x < wh))
            a[x] = tile_px(d, vram16(d, nt + (uint32_t)((line >> 3) * ww + (x >> 3)) * 2), x & 7, line & 7);
}

static void sprites(Md *m, int line, uint8_t *out, int w) {
    Vdp *d = &m->vdp;
    uint32_t sat = h40(m) ? (uint32_t)(R(5) & 0x7E) << 9 : (uint32_t)(R(5) & 0x7F) << 9;
    int max = h40(m) ? 80 : 64, per_line = h40(m) ? 20 : 16, dots = w;  /* dots: sprite pixels a line can fetch */
    int n = 0, masked = 0, shown = 0, idx = 0;
    memset(out, 0, (size_t)w);
    for (int count = 0; count < max; count++) {
        uint32_t e = sat + (uint32_t)idx * 8;
        int y = (vram16(d, e) & 0x1FF) - 128, size = d->vram[(e + 2) & 0xFFFF];
        int wc = (size >> 2 & 3) + 1, hc = (size & 3) + 1, link = d->vram[(e + 3) & 0xFFFF] & 0x7F;
        if (line >= y && line < y + hc * 8) {
            if (++n > per_line) { d->spr_status |= 0x40; break; }
            uint16_t attr = vram16(d, e + 4);
            int x = vram16(d, e + 6) & 0x1FF;
            if (x == 0) masked |= shown;  /* x = 0 hides the rest of the line, after another sprite */
            else shown = 1;
            int row = line - y;
            if (attr & 0x1000) row = hc * 8 - 1 - row;
            for (int i = 0; i < wc * 8; i++) {
                if (dots-- <= 0) { d->spr_status |= 0x40; return; }
                int col = attr & 0x0800 ? wc * 8 - 1 - i : i, sx = x - 128 + i;
                if (masked || sx < 0 || sx >= w) continue;
                uint16_t t = (uint16_t)((attr & 0xF800) | ((attr + (col >> 3) * hc + (row >> 3)) & 0x7FF));
                uint8_t p = tile_px(d, t & 0xE7FF, col & 7, row & 7);  /* flips already applied */
                if (!(p & 0xF)) continue;
                if (out[sx] & 0xF) d->spr_status |= 0x20;  /* the first sprite in the list stays in front */
                else out[sx] = p;
            }
        }
        if (!link || link >= max) break;
        idx = link;
    }
}

/* shortcut: a linear DAC; the console's levels are not evenly spaced */
static uint32_t rgb(uint16_t c, int shade) {  /* shade 0 shadow, 1 normal, 2 highlight */
    uint32_t out = 0;
    for (int i = 0; i < 3; i++) {
        unsigned v = c >> (1 + 4 * i) & 7;
        v = shade == 1 ? v * 255 / 7 : shade == 0 ? v * 255 / 14 : 128 + v * 127 / 7;
        out |= v << (16 - 8 * i);
    }
    return out;
}

static void draw_line(Md *m, int line) {
    Vdp *d = &m->vdp;
    int w = h40(m) ? 320 : 256;
    uint32_t *out = d->fb + line * 320;
    uint8_t bg = R(7) & 0x3F;
    if (!display_on(m)) {
        for (int x = 0; x < w; x++) out[x] = rgb(d->cram[bg], 1);
        return;
    }
    uint8_t a[320], b[320], s[320];
    plane(m, line, 1, b, w);
    plane(m, line, 0, a, w);
    window(m, line, a, w);
    sprites(m, line, s, w);
    int sh = R(12) & 8;
    for (int x = 0; x < w; x++) {
        uint8_t A = a[x], B = b[x], S = s[x];
        int op = sh && (S & 0x3E) == 0x3E ? S & 0x3F : 0;  /* palette 3, colours 14 and 15: highlight, shadow */
        if (op) S = 0;
        uint8_t top = S & 0xF && S & 0x80 ? S : A & 0xF && A & 0x80 ? A : B & 0xF && B & 0x80 ? B
                    : S & 0xF ? S : A & 0xF ? A : B & 0xF ? B : bg;
        if (x < 8 && (R(0) & 0x20)) top = bg;  /* the left column blanked */
        int shade = 1;
        if (sh) {
            shade = (A | B) & 0x80 ? 1 : 0;  /* low-priority planes are shadowed */
            if (top == S && (S & 0x80 || (S & 0xF) == 0xE)) shade = 1;  /* nor are high sprites and colour 14 */
            if (op) shade = op == 0x3E ? (shade < 2 ? shade + 1 : 2) : 0;
        }
        out[x] = rgb(d->cram[top & 0x3F], shade);
    }
}

/* ---- the frame ---- */
void vdp_vblank(Md *m) {
    Vdp *d = &m->vdp;
    d->vint = 1;
    update_ipl(m);
    d->frame_w = h40(m) ? 320 : 256;
    d->frame_h = vdp_height(m);
    for (int y = 0; y < d->frame_h; y++) memcpy(d->frame + y * 320, d->fb + y * 320, (size_t)d->frame_w * 4);
    if (R(12) & 2) d->odd ^= 1;
}

void vdp_line_end(Md *m, int line) {
    Vdp *d = &m->vdp;
    int h = vdp_height(m);
    if (line < h) draw_line(m, line);
    /* the counter runs on the picture's lines and the first below; reloaded on the others */
    if (line <= h) {
        if (d->hcount-- == 0) {
            d->hcount = R(10);
            d->hint = 1;
            update_ipl(m);
        }
    } else {
        d->hcount = R(10);
    }
}
