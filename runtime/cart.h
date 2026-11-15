/* Cartridge: iNES loading and mappers.
 * Supported: 0 NROM, 1 MMC1, 2 UxROM, 3 CNROM, 4 MMC3, 7 AxROM, 11 Color
 * Dreams, 66 GxROM (nesdev wiki pages of the same names). Every mapper only
 * sets the bank tables below; reads go through them. */
#ifndef NES_CART_H
#define NES_CART_H

#include <stddef.h>
#include <stdint.h>

enum { MIRROR_HORIZONTAL, MIRROR_VERTICAL, MIRROR_FOUR, MIRROR_SINGLE_LOW, MIRROR_SINGLE_HIGH };

typedef struct Cart {
    uint8_t *prg, *chr;
    uint32_t prg_size, chr_size;
    int chr_ram, mapper, mirroring, four_screen, battery;
    uint8_t prg_ram[0x2000];         /* $6000-$7FFF */
    uint32_t prg_off[4];             /* PRG offset mapped at $8000/$A000/$C000/$E000 */
    uint32_t chr_off[8];             /* CHR offset of each 1 KB PPU page */
    /* mapper registers */
    uint8_t reg[8];                  /* MMC1: control, chr0, chr1, prg; MMC3: R0-R7 */
    uint8_t latch;                   /* simple mappers: last value written */
    uint8_t mmc1_shift, mmc1_count;
    uint64_t mmc1_last_write;        /* CPU cycle (consecutive writes are ignored) */
    uint8_t mmc3_select;
    uint8_t irq_latch, irq_counter, irq_reload, irq_enabled, irq_line;
    uint8_t a12;                     /* PPU address line 12, last seen */
    uint64_t a12_low_since;          /* PPU dot when A12 went low */
} Cart;

/* Returns 0 on success, else writes a message to err. Copies the data. */
int cart_load(Cart *c, const uint8_t *data, size_t size, char *err, size_t errlen);
void cart_free(Cart *c);

/* CPU $4020-$FFFF. open_bus is returned for unmapped addresses. */
uint8_t cart_cpu_read(Cart *c, uint16_t a, uint8_t open_bus);
/* Returns 1 if the PRG mapping may have changed (the bus re-maps its pages). */
int cart_cpu_write(Cart *c, uint16_t a, uint8_t v, uint64_t cpu_cycle);
/* PPU $0000-$1FFF (inline: the PPU fetches every few dots) */
static inline uint8_t cart_chr_read(Cart *c, uint16_t a) { return c->chr[c->chr_off[(a >> 10) & 7] + (a & 0x3FF)]; }
void cart_chr_write(Cart *c, uint16_t a, uint8_t v);
/* PPU $2000-$3EFF -> index into the PPU's 4 KB nametable RAM */
static inline uint16_t cart_nt_index(Cart *c, uint16_t a) {
    a &= 0x0FFF;
    switch (c->mirroring) {
    case MIRROR_VERTICAL: return a & 0x07FF;                         /* $2000=$2800 */
    case MIRROR_HORIZONTAL: return (a & 0x03FF) | ((a & 0x0800) >> 1); /* $2000=$2400 */
    case MIRROR_SINGLE_LOW: return a & 0x03FF;
    case MIRROR_SINGLE_HIGH: return 0x400 | (a & 0x03FF);
    default: return a;                                                /* four-screen */
    }
}
/* Every PPU bus address, with the PPU's dot counter (MMC3 IRQ counts A12 rises). */
void cart_ppu_address(Cart *c, uint16_t a, uint64_t ppu_dot);
/* MMC3: the IRQ could assert at the next A12 rise (the bus then catches up
 * the PPU finely around sprite fetches). */
int cart_irq_armed(const Cart *c);

#endif
