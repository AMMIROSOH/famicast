/* 2C02 PPU, stepped one dot at a time (3 dots per CPU cycle, docs/04).
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
    int scanline, dot;    /* scanline 0-261 (261 = pre-render), dot 0-340 */
    uint8_t odd_frame, rendering_prev;
    uint64_t frame;
    /* background pipeline */
    uint16_t bg_lo, bg_hi, at_lo, at_hi;
    uint8_t nt_byte, at_bits, pt_lo, pt_hi;
    /* sprites selected for the current scanline */
    int sp_count;
    uint8_t sp_x[8], sp_attr[8], sp_lo[8], sp_hi[8];
    uint8_t sp_zero_on_line, sp_zero_next;
    /* sprite evaluation state (dots 1-256) */
    uint8_t sec_oam[32];
    uint8_t ev_n, ev_m, ev_found, ev_done, ev_copying, ev_buf;
    /* outputs */
    uint8_t nmi_out;      /* NMI line level (vblank && ctrl bit 7) */
    uint8_t nmi_cancel;   /* race near vblank start: drop an NMI edge just seen */
    uint8_t vbl_suppress; /* $2002 read one dot before vblank: flag not set */
    uint8_t frame_done;   /* set when vblank starts; cleared by the caller */
    uint8_t fb[256 * 240];/* palette index per pixel, bits 0-5 (+ emphasis later) */
} Ppu;

void ppu_reset(Ppu *p, Cart *cart);
void ppu_step(Ppu *p);
uint8_t ppu_reg_read(Ppu *p, uint16_t reg);   /* reg = 0..7 */
void ppu_reg_write(Ppu *p, uint16_t reg, uint8_t v);
void ppu_oam_dma_write(Ppu *p, uint8_t v);    /* one byte of $4014 DMA */

/* 64 colours as 0x00RRGGBB, generated from an NTSC signal model (palette.c) */
void nes_palette_rgb(uint32_t out[64]);

#endif
