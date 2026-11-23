/* The CPS-1's 68000 side (cps1.h): the memory map, inputs and DIP
 * switches, the CPS-A / CPS-B registers, and the frame's timeline.
 *   $000000-$3FFFFF  program ROM
 *   $800000          players (P1 low byte, P2 high; active low)
 *   $800018-$80001F  system inputs, DIP switches A-C (in the high byte)
 *   $800100-$80013F  CPS-A registers;  $800140-$80017F  CPS-B registers
 *   $800180 / $800188  sound command / fade (for the Z80)
 *   $900000-$92FFFF  graphics RAM;  $FF0000-$FFFFFF  work RAM
 * Anything else reads $FFFF. VBlank is interrupt level 2 (autovector) from
 * line 240, held until acknowledged. Sources: docs/sources.md (CPS-1). */
#include <string.h>

#include "cps1.h"
#include "input_log.h"

static uint16_t be16(const uint8_t *p) { return (uint16_t)(p[0] << 8 | p[1]); }

static void put(uint8_t *p, uint16_t v, unsigned strobe) {
    if (strobe & 2) p[0] = (uint8_t)(v >> 8);
    if (strobe & 1) p[1] = (uint8_t)v;
}

static uint16_t players(const Cps1 *s) {
    uint16_t v = 0;
    for (int p = 0; p < 2; p++) {
        uint16_t pad = s->pad[p], b = 0;
        if (pad & PAD_RIGHT) b |= 0x01;
        if (pad & PAD_LEFT) b |= 0x02;
        if (pad & PAD_DOWN) b |= 0x04;
        if (pad & PAD_UP) b |= 0x08;
        if (pad & PAD_B) b |= 0x10;  /* button 1 */
        if (pad & PAD_A) b |= 0x20;  /* button 2 */
        if (pad & PAD_X) b |= 0x40;  /* button 3 */
        v |= (uint16_t)(b << (p * 8));
    }
    return (uint16_t)~v;
}

static uint8_t system_in(const Cps1 *s) {
    uint8_t v = 0;
    for (int p = 0; p < 2; p++) {
        if (s->pad[p] & PAD_SELECT) v |= (uint8_t)(0x01 << p);  /* coins */
        if (s->pad[p] & PAD_START) v |= (uint8_t)(0x10 << p);
    }
    return (uint8_t)~v;
}

static uint16_t cpsb_read(const Cps1 *s, unsigned off) {
    const Cps1B *b = &s->rom->game->b;
    uint32_t product = (uint32_t)s->mult_a * s->mult_b;
    if (off == b->id_reg) return b->id_val;
    if (off == b->mult_lo) return (uint16_t)product;
    if (off == b->mult_hi) return (uint16_t)(product >> 16);
    return s->cpsb[off >> 1];
}

static void cpsb_write(Cps1 *s, unsigned off, uint16_t v) {
    const Cps1B *b = &s->rom->game->b;
    if (off == b->mult_a) s->mult_a = v;
    else if (off == b->mult_b) s->mult_b = v;
    s->cpsb[off >> 1] = v;
}

static uint16_t rd(Cps1 *s, uint32_t a, unsigned strobe) {
    (void)strobe;
    a &= 0xFFFFFE;
    const Cps1Rom *r = s->rom;
    if (a < 0x400000) return a + 1 < r->size[CPS1_R_MAIN] ? be16(r->region[CPS1_R_MAIN] + a) : 0xFFFF;
    if (a >= 0xFF0000) return be16(s->ram + (a & 0xFFFF));
    if (a >= 0x900000 && a < 0x930000) return be16(s->gfxram + (a - 0x900000));
    if (a == 0x800000) return players(s);
    if (a == 0x800018) return (uint16_t)(system_in(s) << 8 | 0xFF);
    if (a >= 0x80001A && a < 0x800020) return (uint16_t)(s->dip[(a - 0x80001A) >> 1] << 8 | 0xFF);
    if (a >= 0x800140 && a < 0x800180) return cpsb_read(s, a - 0x800140);
    return 0xFFFF;
}

static void wr(Cps1 *s, uint32_t a, uint16_t v, unsigned strobe) {
    a &= 0xFFFFFE;
    if (a >= 0xFF0000) put(s->ram + (a & 0xFFFF), v, strobe);
    else if (a >= 0x900000 && a < 0x930000) put(s->gfxram + (a - 0x900000), v, strobe);
    else if (a >= 0x800100 && a < 0x800140) s->cpsa[(a - 0x800100) >> 1] = v;
    else if (a >= 0x800140 && a < 0x800180) cpsb_write(s, a - 0x800140, v);
    else if (a >= 0x800180 && a < 0x800188) s->sound_cmd = (uint8_t)v;   /* P3: the Z80 reads it */
    else if (a >= 0x800188 && a < 0x800190) s->sound_fade = (uint8_t)v;
}

uint16_t m68k_read(M68k *c, uint32_t a, unsigned fc, unsigned strobe) { (void)fc; return rd(c->bus, a, strobe); }
void m68k_write(M68k *c, uint32_t a, uint16_t v, unsigned fc, unsigned strobe) { (void)fc; wr(c->bus, a, v, strobe); }
void m68k_idle(M68k *c, unsigned clocks) { (void)c, (void)clocks; }
void m68k_aborted(M68k *c, uint32_t a, unsigned fc, unsigned strobe, int write) { (void)c, (void)a, (void)fc, (void)strobe, (void)write; }
int m68k_iack(M68k *c, int level) { (void)level; c->ipl = 0; return -1; }  /* autovector; acknowledging clears it */
void m68k_reset_out(M68k *c) { (void)c; }

void cps1_init(Cps1 *s, const Cps1Rom *r) {
    memset(s, 0, sizeof *s);
    s->rom = r;
    memset(s->dip, 0xFF, sizeof s->dip);  /* every switch off */
    s->cpu.bus = s;
    m68k_reset(&s->cpu);
}

void cps1_set_input(Cps1 *s, int player, uint16_t pad) {
    if (player >= 0 && player < 2) s->pad[player] = pad;
}

void cps1_frame(Cps1 *s) {
    for (s->line = 0; s->line < CPS1_LINES; s->line++) {
        if (s->line == CPS1_VBLANK_LINE) s->cpu.ipl = 2;
        s->line_end += CPS1_LINE_CLOCKS;
        while (s->cpu.cycles < s->line_end && !s->cpu.halted) m68k_step(&s->cpu);
        if (s->cpu.halted) s->cpu.cycles = s->line_end;
    }
}
