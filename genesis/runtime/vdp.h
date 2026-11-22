/* The VDP (genesis/docs/roadmap.md D2): registers, the data and control
 * ports, VRAM, CRAM and VSRAM, the three DMA kinds, the H/V counter, the
 * vertical and horizontal interrupts, and a line renderer (planes A and B
 * with full, per-cell and per-line scrolling, the window, sprites with
 * their limits and masking, priorities, shadow/highlight, H32/H40).
 * Mode 5 only. A line is drawn when the CPUs have run through it (vdp.c). */
#ifndef GENESIS_VDP_H
#define GENESIS_VDP_H

#include <stdint.h>

struct Md;

typedef struct Vdp {
    uint8_t vram[0x10000];   /* big endian: byte 0 is the upper half of word 0 */
    uint16_t cram[64];       /* ----BBB-GGG-RRR- */
    uint16_t vsram[40];
    uint8_t reg[24];
    uint16_t addr;
    uint8_t code;            /* CD5-CD0: target and direction, bit 5 DMA */
    uint8_t pending;         /* the first word of a command was written */
    uint8_t fill;            /* a VRAM fill waits for its data word */
    uint8_t vint, hint;      /* pending (vint is also the status' F flag) */
    uint8_t spr_status;      /* status bits 6 (too many sprites on a line) and 5 (collision) */
    uint8_t odd;             /* interlace: this frame's field */
    int hcount;              /* H interrupt counter */
    uint64_t dma_end;        /* master clock when the running fill or copy ends (the status' DMA bit) */
    int frame_w, frame_h;    /* of `frame`: 256 or 320 by 224 or 240 */
    uint32_t fb[320 * 240];  /* the frame being drawn, 0xRRGGBB */
    uint32_t frame[320 * 240];  /* the last whole frame (copied at vertical blank) */
} Vdp;

int vdp_height(const struct Md *m);  /* lines of picture: 224, or 240 in V30 */
uint16_t vdp_port_read(struct Md *m, uint32_t a);  /* $C00000-$C0000F */
void vdp_port_write(struct Md *m, uint32_t a, uint16_t v);
void vdp_vblank(struct Md *m);      /* the first line below the picture begins */
void vdp_line_end(struct Md *m, int line);  /* the CPUs ran through this line: draw it, count H interrupts */
void vdp_iack(struct Md *m, int level);

#endif
