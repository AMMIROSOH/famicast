/* The Game Boy (DMG): CPU + memory map + timer + interrupts + serial +
 * joypad + OAM DMA + cartridge, and the CPU bus (Pan Docs "Memory Map",
 * "Timer and Divider Registers", "Interrupts", "OAM DMA Transfer").
 * Everything is stepped once per M-cycle, before the CPU's access in that
 * cycle (gb.c tick); the PPU is in ppu.c. */
#ifndef GB_GB_H
#define GB_GB_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "apu.h"
#include "cart.h"
#include "cpu.h"

enum { GB_FRAME_CYCLES = 17556 };  /* M-cycles per frame: 154 lines x 114 */
enum { INT_VBLANK = 1, INT_STAT = 2, INT_TIMER = 4, INT_SERIAL = 8, INT_JOYPAD = 16 };

typedef struct Gb {
    GbCpu cpu;
    GbCart cart;
    uint8_t vram[0x2000], wram[0x2000], oam[0xA0], hram[0x7F];
    uint8_t io[0x80];          /* registers as written: sound ($10-$3F, apu.c), palettes */
    GbApu apu;
    uint8_t ie, ifl;
    /* timer: DIV is the top byte of a 16-bit counter of clocks; TIMA counts
     * falling edges of the bit TAC selects. tima_reload: 1 = TIMA overflowed
     * in the last cycle (reads 0; a write cancels), 2 = this cycle loads TMA */
    uint16_t div;
    uint8_t tima, tma, tac, tima_reload;
    /* serial: an internal-clock transfer ends after 8 bits at 8192 Hz with
     * no partner: SB reads $FF. Bytes sent are kept for the test runner. */
    uint8_t sb, sc;
    int serial_left;           /* M-cycles until the transfer ends; 0 = none */
    char serial_out[4096];
    int serial_n;
    /* joypad: P1 select bits, buttons held (core/input_log.h PAD_* bits) */
    uint8_t p1, pad;
    /* PPU (ppu.c) */
    uint8_t lcdc, stat, scy, scx, ly, lyc, wy, wx, stat_line;
    uint8_t mode;              /* STAT mode while the LCD is on */
    uint8_t wy_hit, win_line;  /* LY reached WY this frame; the window's own line counter */
    int dot, mode3_end;        /* dot in the line (0-455), where this line's mode 3 ends */
    uint8_t fb[160 * 144];     /* the frame being drawn: shades 0 (light) - 3 (dark) */
    uint8_t frame[160 * 144];  /* the last whole frame (copied at vblank; blank while the LCD is off) */
    uint32_t frame_count;      /* frames completed into `frame` */
    /* OAM DMA: a byte per M-cycle for 160, starting a cycle after the write
     * to $FF46; meanwhile the CPU sees only HRAM */
    uint8_t dma_reg, dma_start, dma_on;
    int dma_pos;
    uint16_t dma_src, dma_next;
    uint64_t frame_end;        /* cpu.cycles where the current frame ends */
    uint8_t recomp_on;         /* recompiled code (recomp_rt.h) made from this ROM is linked in */
    uint64_t cycles_native, cycles_interp;  /* where the CPU's cycles went (--coverage) */
    /* ROM code the interpreter ran, by gb_miss_slot (gb_headless --misses
     * for gbrecomp); NULL: not recorded. Not part of save states. */
    uint8_t *misses;
} Gb;

/* Load a ROM (copied) and set the state the boot ROM leaves (DMG). */
int gb_load(Gb *g, const uint8_t *rom, size_t size, char *err, size_t errlen);
void gb_free(Gb *g);
void gb_step(Gb *g);           /* one instruction, interrupt entry or halted cycle */
void gb_run_frame(Gb *g);      /* GB_FRAME_CYCLES M-cycles */
void gb_set_pad(Gb *g, uint8_t pad);  /* PAD_* bits; a new press raises the joypad interrupt */
uint32_t gb_rom_hash(const Gb *g);     /* FNV-1a of the ROM (recompiled code is matched by it) */
/* misses: one slot per (ROM bank, address) for code at $0000-$7FFF */
static inline size_t gb_miss_slots(const Gb *g) { return (size_t)g->cart.rom_size / 0x4000 * 0x8000; }
void gb_misses_write(const Gb *g, FILE *f);  /* "MISS $PC bank B" lines */

/* save states: the whole machine, for this ROM (by hash) and this build's layout */
int gb_state_save(const Gb *g, const char *path);
int gb_state_load(Gb *g, const char *path, char *err, size_t errlen);

/* ppu.c */
void ppu_tick(Gb *g);
int ppu_mode(const Gb *g);
void ppu_stat_update(Gb *g);
void ppu_stat_write(Gb *g, uint8_t v);
void ppu_lcdc_write(Gb *g, uint8_t v);

#endif
