#include "ppu.h"

#include <string.h>

#define RENDERING(p) ((p)->mask & 0x18)

void ppu_reset(Ppu *p, Cart *cart) {
    memset(p, 0, sizeof *p);
    p->cart = cart;
    p->scanline = 261;  /* start on the pre-render line */
}

/* ---- PPU address space ---- */
static uint8_t pal_index(uint16_t a) {
    uint8_t i = a & 0x1F;
    return ((i & 0x13) == 0x10) ? (uint8_t)(i & ~0x10) : i;  /* $3F10/14/18/1C mirror $3F00/04/08/0C */
}

/* Every PPU bus access drives A12 (MMC3 counts its rises, cart.c). */
static inline void bus_address(Ppu *p, uint16_t a) {
    if (cart_is_mmc3(p->cart)) cart_ppu_address(p->cart, a, p->dots);
}

static uint8_t mem_read(Ppu *p, uint16_t a) {
    a &= 0x3FFF;
    bus_address(p, a);
    if (a < 0x2000) {
        uint8_t v = cart_chr_read(p->cart, a);
        if (p->cart->mapper == 9 || p->cart->mapper == 10) cart_chr_latch(p->cart, a);  /* MMC2/MMC4 */
        return v;
    }
    if (a < 0x3F00) return cart_custom_nt(p->cart) ? cart_nt_read(p->cart, p->nt, a) : p->nt[cart_nt_index(p->cart, a)];
    return p->pal[pal_index(a)];
}

static void mem_write(Ppu *p, uint16_t a, uint8_t v) {
    a &= 0x3FFF;
    bus_address(p, a);
    if (a < 0x2000) cart_chr_write(p->cart, a, v);
    else if (a < 0x3F00) {
        if (cart_custom_nt(p->cart)) cart_nt_write(p->cart, p->nt, a, v);
        else p->nt[cart_nt_index(p->cart, a)] = v;
    }
    else p->pal[pal_index(a)] = v & 0x3F;
}

/* ---- loopy scroll increments (nesdev "PPU scrolling") ---- */
static void inc_x(Ppu *p) {
    if ((p->v & 0x001F) == 31) { p->v &= ~0x001F; p->v ^= 0x0400; }
    else p->v++;
}

static void inc_y(Ppu *p) {
    if ((p->v & 0x7000) != 0x7000) { p->v += 0x1000; return; }
    p->v &= ~0x7000;
    int y = (p->v & 0x03E0) >> 5;
    if (y == 29) { y = 0; p->v ^= 0x0800; }
    else if (y == 31) y = 0;
    else y++;
    p->v = (uint16_t)((p->v & ~0x03E0) | (y << 5));
}

static void copy_x(Ppu *p) { p->v = (p->v & ~0x041F) | (p->t & 0x041F); }
static void copy_y(Ppu *p) { p->v = (p->v & ~0x7BE0) | (p->t & 0x7BE0); }

/* ---- background ---- */
static void load_shifters(Ppu *p) {
    p->bg_lo = (p->bg_lo & 0xFF00) | p->pt_lo;
    p->bg_hi = (p->bg_hi & 0xFF00) | p->pt_hi;
    p->at_lo = (p->at_lo & 0xFF00) | ((p->at_bits & 1) ? 0xFF : 0x00);
    p->at_hi = (p->at_hi & 0xFF00) | ((p->at_bits & 2) ? 0xFF : 0x00);
}

/* Background pattern bytes: MMC5 banks them apart from sprites (cart.c). */
static inline uint8_t bg_pattern(Ppu *p, uint16_t a) {
    return p->cart->mapper == 5 ? cart_mmc5_bg_read(p->cart, a, p->v) : mem_read(p, a);
}

static void fetch_step(Ppu *p) {
    uint16_t bg_table = (p->ctrl & 0x10) ? 0x1000 : 0;
    uint16_t fine_y = (p->v >> 12) & 7;
    switch ((p->dot - 1) & 7) {
    case 0:
        load_shifters(p);
        p->nt_byte = mem_read(p, 0x2000 | (p->v & 0x0FFF));
        break;
    case 2: {
        uint8_t at = mem_read(p, 0x23C0 | (p->v & 0x0C00) | ((p->v >> 4) & 0x38) | ((p->v >> 2) & 0x07));
        if (p->v & 0x40) at >>= 4;  /* coarse Y bit 1 */
        if (p->v & 0x02) at >>= 2;  /* coarse X bit 1 */
        p->at_bits = at & 3;
        if (p->cart->mapper == 5) p->at_bits = (uint8_t)cart_mmc5_attr(p->cart, p->v, p->at_bits);
        break;
    }
    case 4: p->pt_lo = bg_pattern(p, (uint16_t)(bg_table + p->nt_byte * 16 + fine_y)); break;
    case 6: p->pt_hi = bg_pattern(p, (uint16_t)(bg_table + p->nt_byte * 16 + fine_y + 8)); break;
    case 7: inc_x(p); break;
    }
}

/* ---- sprites (nesdev "PPU sprite evaluation") ----
 * Dots 1-64 clear secondary OAM; dots 65-256 copy up to 8 in-range sprites,
 * one OAM read on odd dots and one secondary-OAM write on even dots. With 8
 * found, the overflow search increments both n and m (the hardware bug), so
 * it can miss or falsely detect overflow. Dots 257-320 fetch the patterns,
 * 8 dots per slot (garbage nametable/attribute reads, then pattern low/high;
 * empty slots fetch tile $FF), as the MMC3 IRQ counter sees on A12. */
static uint8_t reverse8(uint8_t b) {
    b = (uint8_t)((b & 0xF0) >> 4 | (b & 0x0F) << 4);
    b = (uint8_t)((b & 0xCC) >> 2 | (b & 0x33) << 2);
    return (uint8_t)((b & 0xAA) >> 1 | (b & 0x55) << 1);
}

static int y_in_range(Ppu *p, uint8_t y) {
    int row = p->scanline - y;
    return row >= 0 && row < ((p->ctrl & 0x20) ? 16 : 8);
}

static void evaluation_dot(Ppu *p) {
    int dot = p->dot;
    if (dot <= 64) {
        if (!(dot & 1)) p->sec_oam[(dot - 1) >> 1] = 0xFF;
        return;
    }
    if (dot == 65) {
        p->ev_n = p->ev_m = p->ev_found = 0;
        p->ev_done = p->ev_copying = 0;
        p->sp_zero_next = 0;
    }
    if (dot & 1) {  /* read cycle */
        p->ev_buf = p->oam[(p->ev_n * 4 + p->ev_m) & 0xFF];
        return;
    }
    if (p->ev_done) return;  /* step 4: reads continue, nothing changes */
    if (p->ev_found < 32) {  /* fewer than 8 sprites: copy */
        p->sec_oam[p->ev_found] = p->ev_buf;
        if (!p->ev_copying) {
            if (!y_in_range(p, p->ev_buf)) {
                p->ev_n = (p->ev_n + 1) & 63;
                if (!p->ev_n) p->ev_done = 1;
                return;
            }
            if (p->ev_n == 0) p->sp_zero_next = 1;
            p->ev_copying = 1;
        }
        p->ev_found++;
        if (++p->ev_m == 4) {
            p->ev_m = 0;
            p->ev_copying = 0;
            p->ev_n = (p->ev_n + 1) & 63;
            if (!p->ev_n) p->ev_done = 1;
        }
        return;
    }
    /* 8 sprites found: overflow search (writes to secondary OAM are ignored) */
    if (p->ev_copying) {  /* reading the 3 bytes after a hit */
        if (++p->ev_m == 4) {
            p->ev_m = 0;
            p->ev_n = (p->ev_n + 1) & 63;
        }
        if (--p->ev_copying == 0) p->ev_done = 1;
        return;
    }
    if (y_in_range(p, p->ev_buf)) {
        p->status |= 0x20;
        p->ev_copying = 3;
        if (++p->ev_m == 4) {
            p->ev_m = 0;
            p->ev_n = (p->ev_n + 1) & 63;
        }
    } else {
        p->ev_n = (p->ev_n + 1) & 63;
        p->ev_m = (p->ev_m + 1) & 3;  /* hardware bug: m should stay 0 */
        if (!p->ev_n) p->ev_done = 1;
    }
}

static void sprite_fetch_dot(Ppu *p) {
    int i = (p->dot - 257) >> 3, phase = (p->dot - 257) & 7;
    if (phase == 0 || phase == 2) {  /* garbage nametable / attribute fetch */
        mem_read(p, 0x2000 | (p->v & 0x0FFF));
        return;
    }
    if (phase != 4 && phase != 6) return;
    int h = (p->ctrl & 0x20) ? 16 : 8;
    int n = p->scanline == 261 ? 0 : p->ev_found / 4;
    const uint8_t *s = &p->sec_oam[i * 4];
    uint8_t y = i < n ? s[0] : 0xFF, tile = i < n ? s[1] : 0xFF, attr = i < n ? s[2] : 0xFF;
    int row = (p->scanline - y) & (h - 1);
    if (i < n && (attr & 0x80)) row = h - 1 - row;
    uint16_t addr;
    if (h == 16)
        addr = (uint16_t)(((tile & 1) ? 0x1000 : 0) + (tile & 0xFE) * 16 + (row >= 8 ? 16 : 0) + (row & 7));
    else
        addr = (uint16_t)(((p->ctrl & 0x08) ? 0x1000 : 0) + tile * 16 + row);
    if (phase == 4) {
        p->sp_fetch_lo = mem_read(p, addr);
        return;
    }
    uint8_t lo = p->sp_fetch_lo, hi = mem_read(p, addr + 8);
    if (i < n) {
        if (attr & 0x40) { lo = reverse8(lo); hi = reverse8(hi); }
        p->sp_x[i] = s[3];
        p->sp_attr[i] = attr;
        p->sp_lo[i] = lo;
        p->sp_hi[i] = hi;
    }
    if (i < 7) return;
    p->sp_count = n;
    /* front-most opaque sprite per pixel (lower slot wins), as render_pixel
     * would find it by scanning the 8 slots */
    memset(p->sp_line, 0, sizeof p->sp_line);
    for (int k = n - 1; k >= 0; k--) {
        for (int off = 0; off < 8; off++) {
            int x = p->sp_x[k] + off;
            if (x > 255) break;
            uint8_t px = (uint8_t)(((p->sp_lo[k] >> (7 - off)) & 1) | (((p->sp_hi[k] >> (7 - off)) & 1) << 1));
            if (!px) continue;
            p->sp_line[x] = (uint8_t)(px | ((p->sp_attr[k] & 3) + 4) << 2 | (p->sp_attr[k] & 0x20) |
                                      (k == 0 ? 0x40 : 0));
        }
    }
}

static void render_pixel(Ppu *p) {
    int x = p->dot - 1;
    uint8_t bg = 0, bg_pal = 0, sp = 0, sp_pal = 0, sp_behind = 0, sp_is_zero = 0;

    if ((p->mask & 0x08) && (x >= 8 || (p->mask & 0x02))) {
        uint16_t bit = 0x8000 >> p->fine_x;
        bg = (uint8_t)(((p->bg_lo & bit) ? 1 : 0) | ((p->bg_hi & bit) ? 2 : 0));
        bg_pal = (uint8_t)(((p->at_lo & bit) ? 1 : 0) | ((p->at_hi & bit) ? 2 : 0));
    }
    if (p->sp_count && (p->mask & 0x10) && (x >= 8 || (p->mask & 0x04))) {
        uint8_t s = p->sp_line[x];
        sp = s & 3;
        sp_pal = (s >> 2) & 7;
        sp_behind = s & 0x20;
        sp_is_zero = (s & 0x40) && p->sp_zero_on_line;
    }
    if (sp_is_zero && bg && x != 255) p->status |= 0x40;  /* sprite 0 hit */

    uint8_t idx;
    if (!bg && !sp) idx = 0;
    else if (!bg) idx = (uint8_t)(sp_pal * 4 + sp);
    else if (!sp || sp_behind) idx = (uint8_t)(bg_pal * 4 + bg);
    else idx = (uint8_t)(sp_pal * 4 + sp);

    uint8_t color;
    if (!RENDERING(p) && (p->v & 0x3F00) == 0x3F00) color = p->pal[pal_index(p->v)];  /* palette hack */
    else color = p->pal[idx ? idx : 0];
    if (p->mask & 0x01) color &= 0x30;  /* greyscale */
    p->fb[p->scanline * 256 + x] = color;
}

void ppu_step(Ppu *p) {
    int line = p->scanline, dot = p->dot;
    p->dots++;
    int visible = line < 240, pre = line == 261;
    if (dot == 0 && line <= 240 && p->cart->mapper == 5) cart_mmc5_scanline(p->cart, line, RENDERING(p) != 0);

    if ((visible || pre) && RENDERING(p)) {
        if ((dot >= 2 && dot <= 257) || (dot >= 322 && dot <= 337)) {
            if (p->mask & 0x08) {
                p->bg_lo <<= 1; p->bg_hi <<= 1; p->at_lo <<= 1; p->at_hi <<= 1;
            }
        }
        if ((dot >= 1 && dot <= 256) || (dot >= 321 && dot <= 336)) fetch_step(p);
        if (dot == 256) inc_y(p);
        if (dot == 257) { load_shifters(p); copy_x(p); }
        if (pre && dot >= 280 && dot <= 304) copy_y(p);
        if (visible && dot >= 1 && dot <= 256) evaluation_dot(p);
        if (pre && dot == 257) p->sp_zero_next = 0;
        if (dot >= 257 && dot <= 320) sprite_fetch_dot(p);
        if (dot == 337 || dot == 339) mem_read(p, 0x2000 | (p->v & 0x0FFF));  /* unused NT fetches */
    }
    if (visible && dot >= 1 && dot <= 256) render_pixel(p);
    if (dot == 340) {  /* sprites evaluated on this line are drawn on the next */
        if (!RENDERING(p) || !(visible || pre)) p->sp_count = 0, p->sp_zero_next = 0;
        p->sp_zero_on_line = p->sp_zero_next;
    }

    if (line == 241 && dot == 1) {
        if (!p->vbl_suppress) p->status |= 0x80;
        p->vbl_suppress = 0;
        p->frame_done = 1;
    }
    if (pre && dot == 1) p->status &= 0x1F;  /* clear vblank, sprite 0 hit, overflow */
    p->nmi_out = (p->status & 0x80) && (p->ctrl & 0x80);

    /* advance; odd frames skip the last dot of the pre-render line when rendering */
    /* the skip uses the rendering-enable state as of the previous dot (blargg 10-even_odd_timing) */
    if (pre && dot == 339 && p->odd_frame && p->rendering_prev) p->dot = 340;
    p->rendering_prev = RENDERING(p) != 0;
    if (++p->dot > 340) {
        p->dot = 0;
        if (++p->scanline > 261) {
            p->scanline = 0;
            p->odd_frame ^= 1;
            p->frame++;
        }
    }
}

/* Dots 1-256 of a visible line with rendering on: what ppu_step does there,
 * without its checks for other lines and dots. Nothing the checks guard can
 * change inside one ppu_run call (no CPU access in between): mask, ctrl and
 * the vblank flag stay as they are. */
static void visible_dots(Ppu *p, int n) {
    /* render_pixel with the mask decoded once; pixels go to a local line
     * first (stores into p->fb could alias every PPU field) */
    const uint8_t mask = p->mask;
    const int bg_on = mask & 0x08, bg_left = mask & 0x02;
    const int sp_on = (mask & 0x10) && p->sp_count, sp_left = mask & 0x04;
    const uint8_t grey = (mask & 0x01) ? 0x30 : 0x3F;
    const int first = p->dot - 1;
    uint8_t line[256];
    for (; n > 0; n--) {
        int dot = p->dot, x = dot - 1;
        p->dots++;
        if (bg_on && dot >= 2) {
            p->bg_lo <<= 1; p->bg_hi <<= 1; p->at_lo <<= 1; p->at_hi <<= 1;
        }
        fetch_step(p);
        if (dot == 256) inc_y(p);
        evaluation_dot(p);
        uint8_t bg = 0, bg_pal = 0;
        if (bg_on && (x >= 8 || bg_left)) {
            uint16_t bit = 0x8000 >> p->fine_x;
            bg = (uint8_t)(((p->bg_lo & bit) ? 1 : 0) | ((p->bg_hi & bit) ? 2 : 0));
            bg_pal = (uint8_t)(((p->at_lo & bit) ? 1 : 0) | ((p->at_hi & bit) ? 2 : 0));
        }
        uint8_t s = (sp_on && (x >= 8 || sp_left)) ? p->sp_line[x] : 0, sp = s & 3;
        if ((s & 0x40) && p->sp_zero_on_line && bg && x != 255) p->status |= 0x40;  /* sprite 0 hit */
        uint8_t idx = (!sp || (bg && (s & 0x20))) ? (uint8_t)(bg ? bg_pal * 4 + bg : 0)
                                                    : (uint8_t)(((s >> 2) & 7) * 4 + sp);
        line[x] = p->pal[idx] & grey;
        p->dot = dot + 1;
    }
    memcpy(p->fb + p->scanline * 256 + first, line + first, (size_t)(p->dot - 1 - first));
    p->rendering_prev = 1;
    p->nmi_out = (p->status & 0x80) && (p->ctrl & 0x80);
}

/* Lines 241 (from dot 2) to 260 have no per-dot effects: no rendering, no
 * fetches, the flags only change at (241,1) and (261,1), and the end-of-line
 * sprite reset is a no-op after line 240. Skip them in one step. */
void ppu_run(Ppu *p, int k) {
    while (k > 0) {
        int line = p->scanline;
        if ((line == 241 && p->dot >= 2) || (line > 241 && line <= 260)) {
            int left = (261 - line) * 341 - p->dot;  /* dots until (261, 0) */
            int n = k < left ? k : left;
            int pos = line * 341 + p->dot + n;
            p->scanline = pos / 341;
            p->dot = pos % 341;
            p->rendering_prev = RENDERING(p) != 0;
            p->nmi_out = (p->status & 0x80) && (p->ctrl & 0x80);
            p->dots += (uint64_t)n;
            k -= n;
            continue;
        }
        if (line < 240 && p->dot >= 1 && p->dot <= 256 && RENDERING(p)) {
            int n = 257 - p->dot;
            if (n > k) n = k;
            visible_dots(p, n);
            k -= n;
            continue;
        }
        ppu_step(p);
        k--;
    }
}

/* PPU open bus: each latch bit decays to 0 when not driven for ~600 ms
 * (nesdev "Open bus behavior"; blargg ppu_open_bus). Reads drive only some bits. */
enum { LATCH_DECAY_FRAMES = 36 };

static void latch_drive(Ppu *p, uint8_t v, uint8_t bits) {
    p->latch = (uint8_t)((p->latch & ~bits) | (v & bits));
    for (int b = 0; b < 8; b++)
        if (bits >> b & 1) p->latch_frame[b] = p->frame;
}

static uint8_t latch_value(Ppu *p) {
    for (int b = 0; b < 8; b++)
        if (p->frame - p->latch_frame[b] > LATCH_DECAY_FRAMES) p->latch &= (uint8_t)~(1 << b);
    return p->latch;
}

uint8_t ppu_reg_read(Ppu *p, uint16_t reg) {
    switch (reg & 7) {
    case 2:
        /* Races at vblank start (nesdev "PPU frame timing", blargg 06-suppression):
         * read one dot early -> flag reads clear and is never set, no NMI;
         * read on the set dot or the next one -> flag reads set, but no NMI. */
        if (p->scanline == 241 && p->dot == 1) p->vbl_suppress = 1;
        if (p->scanline == 241 && (p->dot == 2 || p->dot == 3)) p->nmi_cancel = 1;
        latch_drive(p, (uint8_t)((p->status & 0xE0) | (latch_value(p) & 0x1F)), 0xE0);
        p->status &= ~0x80;
        p->w = 0;
        break;
    case 4: {
        uint8_t v = p->oam[p->oamaddr];
        if ((p->oamaddr & 3) == 2) v &= 0xE3;  /* unused attribute bits read as 0 */
        latch_drive(p, v, 0xFF);
        break;
    }
    case 7: {
        uint16_t a = p->v & 0x3FFF;
        if (a < 0x3F00) {
            latch_drive(p, p->read_buf, 0xFF);
            p->read_buf = mem_read(p, a);
        } else {  /* palette reads are not buffered; buffer gets the nametable below */
            latch_drive(p, mem_read(p, a), 0x3F);  /* top 2 bits stay open bus */
            p->read_buf = mem_read(p, a - 0x1000);
        }
        p->v = (p->v + ((p->ctrl & 0x04) ? 32 : 1)) & 0x7FFF;
        bus_address(p, p->v & 0x3FFF);  /* the bus then shows the new address (MMC3 A12) */
        break;
    }
    default: break;  /* write-only registers return the latch */
    }
    return latch_value(p);
}

void ppu_reg_write(Ppu *p, uint16_t reg, uint8_t v) {
    latch_drive(p, v, 0xFF);
    switch (reg & 7) {
    case 0:
        /* disabling NMI right after vblank starts still prevents it (blargg 08) */
        if (!(v & 0x80) && p->scanline == 241 && (p->dot == 2 || p->dot == 3)) p->nmi_cancel = 1;
        p->ctrl = v;
        if (p->cart->mapper == 5) cart_mmc5_ppuctrl(p->cart, v);  /* MMC5 snoops the sprite size */
        p->t = (uint16_t)((p->t & 0xF3FF) | ((v & 3) << 10));
        p->nmi_out = (p->status & 0x80) && (p->ctrl & 0x80);
        break;
    case 1: p->mask = v; break;
    case 3: p->oamaddr = v; break;
    case 4: p->oam[p->oamaddr++] = v; break;
    case 5:
        if (!p->w) { p->t = (p->t & 0xFFE0) | (v >> 3); p->fine_x = v & 7; }
        else p->t = (uint16_t)((p->t & 0x8C1F) | ((v & 7) << 12) | ((v & 0xF8) << 2));
        p->w ^= 1;
        break;
    case 6:
        if (!p->w) p->t = (uint16_t)((p->t & 0x00FF) | ((v & 0x3F) << 8));
        else { p->t = (p->t & 0xFF00) | v; p->v = p->t; bus_address(p, p->v & 0x3FFF); }
        p->w ^= 1;
        break;
    case 7:
        mem_write(p, p->v, v);
        p->v = (p->v + ((p->ctrl & 0x04) ? 32 : 1)) & 0x7FFF;
        bus_address(p, p->v & 0x3FFF);
        break;
    default: break;
    }
}

void ppu_oam_dma_write(Ppu *p, uint8_t v) { p->oam[p->oamaddr++] = v; }
