#include "runtime.h"

/* ppu.c — logic-only PPU status model (2A03, see docs/04-timing-constants.md)
 *
 * Position model: frame-absolute PPU cycles, 341 per line, 262 lines/frame.
 * $2002 read:  V (line 241..260), S0H (stub 0), SP (sprite overflow, reset
 *   on read).  $2002 write: clears SP/S0H (2A03 behaviour — vblank is NOT
 *   cleared, which is why the classic "write $2002 to reset vblank" hack
 *   relies on 2C02 and is avoided here).
 */

static int sprite_count_at_line(unsigned int line) {
    int n = 0;
    for (int i = 0; i < 32; i++) {
        unsigned char y = rt_ppu.oam[i * 4];      /* Y is the first OAM byte */
        if (line <= 239 && line >= (int)y - 1 && line <= (int)y + 2) n++;
    }
    return n;
}

unsigned char rt_ppu_read(unsigned short a) {
    /* frame-relative: pos grows unbounded across frames */
    unsigned long long fpos = rt_ppu.pos % PPU_FRAME;
    unsigned int line = (unsigned int)(fpos / PPU_LINE);
    if (a == 0x2002) {
        unsigned char v = 0;
        if (line >= 241 && line < 261) v |= 0x80;          /* vblank */
        if (rt_ppu.s0_hit) v |= 0x40;
        if (line < 240)
            rt_ppu.sp_ovf = sprite_count_at_line(line) >= 8;
        if (rt_ppu.sp_ovf) v |= 0x20;
        rt_ppu.sp_ovf = 0;                                  /* 2A02 read-reset */
        return v;
    }
    if (a == 0x2004) {                                     /* OAM read */
        unsigned char v = rt_ppu.oam[rt_ppu.oamaddr & 0xFF];
        rt_ppu.oamaddr = (unsigned char)((rt_ppu.oamaddr + 1) & 0xFF);
        return v;
    }
    return 0;                                              /* everything else */
}

void rt_ppu_write(unsigned short a, unsigned char v) {
    unsigned int cyc = (unsigned int)(rt_ppu.pos % PPU_LINE);
    (void)cyc; /* tile/sprite latches not needed at this tier */
    switch (a) {
    case 0x2000: rt_ppu.puctl = v; break;
    case 0x2001: rt_ppu.pumask = v; break;
    case 0x2002: rt_ppu.sp_ovf = 0; rt_ppu.s0_hit = 0; break;  /* 2A03 */
    case 0x2003: rt_ppu.ptr = (unsigned char)((rt_ppu.ptr + 1) & 7); break;
    case 0x2004: rt_ppu.oam[rt_ppu.oamaddr & 0xFF] = v; break; /* no oamaddr++ on write */
    case 0x2005: break;                                     /* scroll latches */
    case 0x2006: rt_ppu.oamaddr = v; break;
    default: break;                                          /* APU: no-op here */
    }
}
