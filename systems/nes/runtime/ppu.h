/* 2C02 PPU (and the PAL 2C07 and Dendy timings: ppu_reset), stepped one dot
 * at a time (3 dots per CPU cycle, 3.2 on PAL; systems/nes/docs/timing-constants.md).
 * References: nesdev wiki "PPU registers", "PPU scrolling" (loopy v/t/x/w),
 * "PPU rendering", "PPU sprite evaluation", "PPU frame timing". */
#ifndef NES_PPU_H
#define NES_PPU_H

#include <stdint.h>

#include "cart.h"

typedef struct Ppu {
    Cart *cart;
    /* registers */
    uint8_t ctrl, mask, status, oamaddr;
    uint8_t latch;      /* PPU I/O open bus (last value written/read) */
    uint64_t latch_frame[8];  /* frame each latch bit was last driven (decay) */
    uint8_t read_buf;   /* $2007 read buffer */
    uint16_t v, t;      /* loopy current / temporary VRAM address */
    uint8_t fine_x, w;  /* fine X scroll, $2005/$2006 write toggle */
    /* memory */
    uint8_t nt[0x1000];   /* nametable RAM (2 KB used unless four-screen) */
    uint8_t pal[32];
    uint8_t oam[256];
    /* timing */
    int scanline, dot;    /* scanline 0-pre_line, dot 0-340 */
    int vbl_line, pre_line;  /* NTSC 241, 261; PAL 241, 311; Dendy 291, 311 */
    uint8_t odd_frame, rendering_prev;
    uint64_t frame;
    uint64_t dots;        /* PPU dots since power-on (MMC3 A12 filter) */
    /* background pipeline */
    uint16_t bg_lo, bg_hi, at_lo, at_hi;
    uint8_t nt_byte, at_bits, pt_lo, pt_hi;
    /* sprites selected for the current scanline */
    int sp_count;
    uint8_t sp_x[8], sp_attr[8], sp_lo[8], sp_hi[8];
    /* per-pixel sprite result for the current line, built at fetch time:
     * bits 0-1 pixel, 2-4 palette (4-7), 5 behind background, 6 sprite 0 */
    uint8_t sp_line[256];
    uint8_t sp_fetch_lo;
    uint8_t sp_zero_on_line, sp_zero_next;
    /* sprite evaluation state (dots 1-256) */
    uint8_t sec_oam[32];
    uint8_t ev_n, ev_m, ev_found, ev_done, ev_copying, ev_buf;
    /* outputs */
    uint8_t nmi_out;      /* NMI line level (vblank && ctrl bit 7) */
    uint8_t nmi_cancel;   /* race near vblank start: drop an NMI edge just seen */
    uint8_t vbl_suppress; /* $2002 read one dot before vblank: flag not set */
    uint8_t frame_done;   /* set when vblank starts; cleared by the caller */
    uint16_t fb[256 * 240];  /* per pixel: bits 0-5 colour, 6-8 emphasis ($2001 bits 5-7) */
} Ppu;

/* 1: draw every sprite on a line, not only the first 8 (less flicker; a
 * display option, not console state, so not in save states) */
extern int ppu_all_sprites;

void ppu_reset(Ppu *p, Cart *cart);
void ppu_step(Ppu *p);
void ppu_run(Ppu *p, int dots);  /* = dots x ppu_step, skipping idle vblank lines */
uint8_t ppu_reg_read(Ppu *p, uint16_t reg);   /* reg = 0..7 */
void ppu_reg_write(Ppu *p, uint16_t reg, uint8_t v);
void ppu_oam_dma_write(Ppu *p, uint8_t v);    /* one byte of $4014 DMA */

/* 64 colours as 0x00RRGGBB, generated from an NTSC signal model (palette.c) */
void nes_palette_rgb(uint32_t out[512]);  /* RGB of fb values: emphasis << 6 | colour */

#endif
