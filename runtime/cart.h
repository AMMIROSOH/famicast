/* Cartridge: iNES loading and mappers.
 * Supported: 0 NROM, 1 MMC1, 2 UxROM, 3 CNROM, 4 MMC3, 7 AxROM, 11 Color
 * Dreams, 66 GxROM, 71 Camerica, 79 NINA-03/06, 87 Jaleco J87, 185 CNROM
 * with CHR protection, 206 Namco 108, 21/22/23/25 Konami VRC2/VRC4, 5 MMC5,
 * 19 Namco 163, 9 MMC2, 10 MMC4, 18 Jaleco SS88006, 69 Sunsoft FME-7, 210
 * Namco 175/340, 16/153/157/159 Bandai FCG, 118 TxSROM, 75 Konami VRC1,
 * 32 Irem G-101, 33/48 Taito TC0190/TC0690, 80/207 Taito X1-005 (nesdev
 * wiki, see cart.c). Every mapper only sets the bank tables below; reads
 * go through them. MMC5 and Namco 163 also map the nametables
 * (cart_nt_read), and MMC5 has its own background banks. */
#ifndef NES_CART_H
#define NES_CART_H

#include <stddef.h>
#include <stdint.h>

enum { MIRROR_HORIZONTAL, MIRROR_VERTICAL, MIRROR_FOUR, MIRROR_SINGLE_LOW, MIRROR_SINGLE_HIGH };

/* An I2C serial EEPROM (Bandai boards: 24C02, or X24C01 with no device byte),
 * its bytes in Cart.prg_ram at `base` (battery saves keep them). */
typedef struct {
    uint8_t scl, sda;                /* the lines as last driven */
    uint8_t state, next, bit, shift, out;  /* out: the chip's SDA (0 = pulled low) */
    uint8_t addr, c01;               /* c01: X24C01 (7-bit address, bits LSB first) */
    uint16_t base, size;
} Eeprom;

typedef struct Cart {
    uint8_t *prg, *chr;
    uint32_t prg_size, chr_size;
    int chr_ram, mapper, submapper, mirroring, four_screen, battery;  /* submapper: NES 2.0, else 0 */
    uint8_t prg_ram[0x10000];        /* $6000-$7FFF: 8 KB, MMC5 up to 64 KB in 8 KB banks */
    uint32_t prg_ram_size, ram_off;  /* ram_off: the bank at $6000 (battery saves: prg_ram_size) */
    uint8_t rom_at_6000;             /* FME-7: $6000 maps PRG ROM at ram_off, not RAM */
    uint32_t prg_off[4];             /* PRG offset mapped at $8000/$A000/$C000/$E000 */
    uint8_t prg_ram_window;          /* bit w: window w maps PRG RAM, prg_off[w] into prg_ram (MMC5) */
    uint32_t chr_off[8];             /* CHR offset of each 1 KB PPU page */
    /* mapper registers */
    uint8_t reg[8];                  /* MMC1: control, chr0, chr1, prg; MMC3: R0-R7 */
    uint8_t latch;                   /* simple mappers: last value written */
    uint8_t mmc1_shift, mmc1_count;
    uint64_t mmc1_last_write;        /* CPU cycle (consecutive writes are ignored) */
    uint8_t mmc3_select;
    uint8_t txsrom_nt[4];            /* TxSROM (118): CIRAM page of each nametable */
    uint8_t irq_latch, irq_counter, irq_reload, irq_enabled, irq_line;
    uint64_t irq_due_dot;            /* TC0690 (48): PPU dot its delayed IRQ asserts at, 0 = none */
    uint8_t a12;                     /* PPU address line 12, last seen */
    uint64_t a12_low_since;          /* PPU dot when A12 went low */
    /* VRC2/VRC4 (IRQ latch, counter and line: the irq_ fields above) */
    uint16_t vrc_chr[8];             /* 1 KB CHR banks */
    uint8_t vrc_prg[2], vrc_swap, vrc2;  /* vrc_swap: $8000 and $C000 exchanged; vrc2: no IRQ */
    uint8_t vrc_irq_ctrl;            /* bit 0 A (E after ack), 1 E (enabled), 2 M (cycle mode) */
    int16_t vrc_prescaler;           /* scanline mode: 341, minus 3 per CPU cycle */
    uint64_t irq_cycle;              /* VRC4, Namco 163: CPU cycle the IRQ counter has run to */
    /* MMC5 */
    uint8_t mmc5_prg_mode, mmc5_chr_mode, mmc5_exram_mode, mmc5_nt, mmc5_fill_tile, mmc5_fill_attr;
    uint8_t mmc5_prg[5];             /* $5113 (RAM at $6000), $5114-$5117 */
    uint16_t mmc5_chr[12];           /* $5120-$5127 (set A), $5128-$512B (set B), with $5130 bits */
    uint8_t mmc5_chr_upper, mmc5_last_b, mmc5_sprite16;  /* sprite16: from $2000 writes */
    uint8_t mmc5_irq_compare, mmc5_irq_enable, mmc5_irq_pending, mmc5_in_frame, mmc5_counter;
    uint8_t mmc5_mul[2];
    uint8_t exram[0x400];
    uint32_t chr_off_bg[8];          /* background CHR pages (set B with 8x16 sprites) */
    /* Namco 163 */
    uint8_t n163_chr[8], n163_nt[4], n163_prg[3];
    uint8_t n163_ram[128], n163_addr; /* sound/data RAM behind $4800, address at $F800 */
    uint16_t n163_counter;           /* 15-bit IRQ counter, counts CPU cycles up to $7FFF */
    uint8_t n163_irq_enable;
    /* its wavetable sound: one channel updated every 15 CPU cycles (cart_n163_clock) */
    uint8_t n163_sound;              /* mapper 19: the sound runs (heard unless submapper 1, 2) */
    uint8_t n163_halt;               /* $E000 bit 6: sound halted, output 0 */
    uint8_t n163_div, n163_ch;       /* cycles since the last update; next channel (0 = channel 8) */
    int8_t n163_out[8];              /* each channel's last output, (sample - 8) * volume */
    /* MMC2/MMC4 (9, 10), SS88006 (18), FME-7 (69), VRC1 (75), G-101 (32) */
    uint8_t prg_bank[3];             /* 8 KB banks at $8000/$A000/$C000 (MMC2/MMC4: [0] only) */
    uint8_t chr_bank[8];             /* 1 KB banks (MMC2/MMC4: four 4 KB, $B000-$E000; VRC1: two 4 KB) */
    uint8_t chr_latch[2];            /* MMC2/MMC4: 0 = $FD, 1 = $FE, per pattern table */
    uint8_t fme7_cmd, fme7_6000, irq_ctrl;  /* irq_ctrl: FME-7 command $D, SS88006 $F001 */
    uint16_t irq_count16, irq_reload16;     /* 16-bit down counters (SS88006 reload: $E000-$E003) */
    /* Bandai FCG (16, 153, 157, 159): prg_bank[0], chr_bank, irq_ctrl/count16/reload16 above */
    Eeprom eeprom[2];                /* [0] on the board, [1] Datach cartridge (157) */
    uint8_t eeprom_count;
} Cart;

/* Returns 0 on success, else writes a message to err. Copies the data. */
int cart_load(Cart *c, const uint8_t *data, size_t size, char *err, size_t errlen);
void cart_free(Cart *c);

/* CPU $4020-$FFFF. open_bus is returned for unmapped addresses. */
uint8_t cart_cpu_read(Cart *c, uint16_t a, uint8_t open_bus);
/* Is a CPU write to `a` a mapper register? (The bus catches the PPU up first:
 * bank switches can be raster effects.) Mapper 79 decodes $4100 with A8 set
 * ($4100-$5FFF), mapper 87 $6000-$7FFF; the rest $8000-$FFFF. */
static inline int cart_is_reg(const Cart *c, uint16_t a) {
    if (a >= 0x8000) return c->mapper != 0;
    if (c->mapper == 79) return (a & 0xE100) == 0x4100;
    if (c->mapper == 5) return a >= 0x5000 && a < 0x6000;
    if (c->mapper == 19) return a >= 0x4800 && a < 0x6000;
    if (c->mapper == 16 && c->submapper != 5) return a >= 0x6000;  /* FCG-1/2 */
    if (c->mapper == 80 || c->mapper == 207) return (a & 0xFF70) == 0x7E70;  /* X1-005: $7EF0-$7EFF, A7 ignored */
    return c->mapper == 87 && a >= 0x6000;
}
/* $6000-$7FFF is no memory: a register or an EEPROM line (reads and writes
 * take the bus's slow path). */
static inline int cart_6000_is_io(const Cart *c) {
    return c->mapper == 87 || c->mapper == 16 || c->mapper == 157 || c->mapper == 159 || c->mapper == 80 ||
           c->mapper == 207;
}
/* MMC3's IRQ counter: MMC3, TxSROM (an MMC3 board wiring CHR bank bit 7 to
 * the nametables) and Taito TC0690 (48) */
static inline int cart_is_mmc3(const Cart *c) { return c->mapper == 4 || c->mapper == 118 || c->mapper == 48; }
/* A CPU read the PPU must be caught up for first (MMC5 scanline IRQ status). */
static inline int cart_read_syncs(const Cart *c, uint16_t a) { return c->mapper == 5 && a == 0x5204; }
/* Returns 1 if the PRG mapping may have changed (the bus re-maps its pages). */
int cart_cpu_write(Cart *c, uint16_t a, uint8_t v, uint64_t cpu_cycle);
/* PPU $0000-$1FFF (inline: the PPU fetches every few dots) */
static inline uint8_t cart_chr_read(Cart *c, uint16_t a) { return c->chr[c->chr_off[(a >> 10) & 7] + (a & 0x3FF)]; }
void cart_chr_write(Cart *c, uint16_t a, uint8_t v);
/* MMC5, Namco 163, TxSROM and X1-005 207 map each 1 KB nametable themselves:
 * cart_nt_read/write (ciram = the PPU's nametable RAM). The rest use cart_nt_index. */
static inline int cart_custom_nt(const Cart *c) {
    return c->mapper == 5 || c->mapper == 19 || c->mapper == 118 || c->mapper == 207;
}
uint8_t cart_nt_read(Cart *c, const uint8_t *ciram, uint16_t a);
void cart_nt_write(Cart *c, uint8_t *ciram, uint16_t a, uint8_t v);
/* MMC5 background fetches (v = the PPU's VRAM address, for ExRAM's per-tile
 * banks and palettes), the scanline IRQ (called at dot 0 of lines 0-240) and
 * the sprite size it snoops from $2000 writes. */
/* MMC2/MMC4: a PPU read of tile $FD/$FE's pattern switches CHR (after the
 * read: the PPU calls this once it has the byte). */
void cart_chr_latch(Cart *c, uint16_t a);
uint8_t cart_mmc5_bg_read(Cart *c, uint16_t a, uint16_t v);
int cart_mmc5_attr(const Cart *c, uint16_t v, int at_bits);
void cart_mmc5_scanline(Cart *c, int line, int rendering);
void cart_mmc5_ppuctrl(Cart *c, uint8_t ctrl);
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
/* VRC4, Namco 163, SS88006, FME-7, Bandai FCG: the IRQ counter counts CPU cycles. cart_run runs it to `cycle` (the
 * bus calls it before reading irq_line); cart_cycles_to_irq says how many
 * cycles after `cycle` it fires next, -1 if it is off. */
void cart_run(Cart *c, uint64_t cycle);
/* Namco 163 sound, run by the APU every CPU cycle: returns 1 when the output
 * changed; cart_n163_level is the output, the channels averaged. */
int cart_n163_clock(Cart *c);
float cart_n163_level(const Cart *c);
int64_t cart_cycles_to_irq(const Cart *c, uint64_t cycle);

#endif
