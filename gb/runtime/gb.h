/* The Game Boy (DMG): CPU + memory map + timer + interrupts + serial +
 * joypad + OAM DMA + cartridge, and the CPU bus (Pan Docs "Memory Map",
 * "Timer and Divider Registers", "Interrupts", "OAM DMA Transfer").
 * Everything is stepped once per M-cycle, before the CPU's access in that
 * cycle (gb.c tick).
 * shortcut: the PPU is only its timing (LY, the STAT mode, LYC, the
 * vblank and STAT interrupts) until G3 draws the picture (docs/05). */
#ifndef GB_GB_H
#define GB_GB_H

#include <stddef.h>
#include <stdint.h>

#include "cart.h"
#include "cpu.h"

enum { GB_FRAME_CYCLES = 17556 };  /* M-cycles per frame: 154 lines x 114 */
enum { INT_VBLANK = 1, INT_STAT = 2, INT_TIMER = 4, INT_SERIAL = 8, INT_JOYPAD = 16 };

typedef struct Gb {
    GbCpu cpu;
    GbCart cart;
    uint8_t vram[0x2000], wram[0x2000], oam[0xA0], hram[0x7F];
    uint8_t io[0x80];          /* registers without behaviour of their own (sound until G4, palettes) */
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
    /* LCD timing */
    uint8_t lcdc, stat, scy, scx, ly, lyc, wy, wx, stat_line;
    int line_cycle;            /* M-cycles into the line (114 per line) */
    /* OAM DMA: a byte per M-cycle for 160, starting a cycle after the write
     * to $FF46; meanwhile the CPU sees only HRAM */
    uint8_t dma_reg, dma_start, dma_on;
    int dma_pos;
    uint16_t dma_src, dma_next;
    uint64_t frame_end;        /* cpu.cycles where the current frame ends */
} Gb;

/* Load a ROM (copied) and set the state the boot ROM leaves (DMG). */
int gb_load(Gb *g, const uint8_t *rom, size_t size, char *err, size_t errlen);
void gb_free(Gb *g);
void gb_step(Gb *g);           /* one instruction, interrupt entry or halted cycle */
void gb_run_frame(Gb *g);      /* GB_FRAME_CYCLES M-cycles */
void gb_set_pad(Gb *g, uint8_t pad);  /* PAD_* bits; a new press raises the joypad interrupt */

#endif
