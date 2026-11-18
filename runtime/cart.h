/* Cartridge: iNES loading and mappers.
 * Supported: 0 NROM, 1 MMC1, 2 UxROM, 3 CNROM, 4 MMC3, 7 AxROM, 11 Color
 * Dreams, 66 GxROM, 71 Camerica, 79 NINA-03/06, 87 Jaleco J87, 185 CNROM
 * with CHR protection, 206 Namco 108, 21/22/23/25 Konami VRC2/VRC4, 5 MMC5,
 * 19 Namco 163, 9 MMC2, 10 MMC4, 18 Jaleco SS88006, 69 Sunsoft FME-7, 210
 * Namco 175/340, 16/153/157/159 Bandai FCG, 118 TxSROM, 75 Konami VRC1,
 * 32 Irem G-101, 33/48 Taito TC0190/TC0690, 80/207 Taito X1-005, 24/26
 * Konami VRC6, 85 Konami VRC7, the boards in cart.c's "more boards"
 * section (nesdev wiki, see cart.c), and the Famicom Disk System as 20
 * (fds.c). Every mapper only sets the bank tables below; reads
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

/* TV system (nesdev "Cycle reference chart"): NTSC, PAL (2A07/2C07) or
 * Dendy (the UA6538 famiclones: PAL video, NTSC-like CPU and APU) */
enum { REGION_NTSC = 0, REGION_PAL = 1, REGION_DENDY = 3 };

/* FDS: bytes a side in an image; a side's room with gaps and block marks */
enum { FDS_SIDE = 65500, FDS_RAW_SIDE = 0x18000, FDS_MAX_SIDES = 16 };

typedef struct Cart {
    uint8_t *prg, *chr;
    uint32_t prg_size, chr_size;     /* chr_size: CHR ROM then CHR RAM (77, 119 have both) */
    uint32_t chr_rom_size;           /* CHR below this offset is ROM (writes ignored) */
    int chr_ram, mapper, submapper, mirroring, four_screen, battery;  /* submapper: NES 2.0, else 0 */
    int region;                      /* REGION_*: NES 2.0 byte 12, iNES byte 9 bit 0 */
    uint8_t prg_ram[0x10000];        /* $6000-$7FFF: 8 KB, MMC5 up to 64 KB in 8 KB banks */
    uint32_t prg_ram_size, ram_off;  /* ram_off: the bank at $6000 (battery saves: prg_ram_size) */
    uint8_t rom_at_6000;             /* FME-7: $6000 maps PRG ROM at ram_off, not RAM */
    uint32_t prg_off[4];             /* PRG offset mapped at $8000/$A000/$C000/$E000 */
    uint8_t prg_ram_window;          /* bit w: window w maps PRG RAM, prg_off[w] into prg_ram (MMC5) */
    uint32_t chr_off[8];             /* CHR offset of each 1 KB PPU page */
    /* mapper registers */
    uint8_t reg[16];                 /* MMC1: control, chr0, chr1, prg; MMC3: R0-R7; RAMBO-1: R0-RF */
    uint8_t latch;                   /* simple mappers: last value written */
    uint8_t outer, ram_ctrl;         /* multicart outer bank (37, 41, 47, 232), MMC3 $A001 */
    uint16_t addr_latch;             /* mappers that latch the address written (228) */
    uint8_t irq_toggle, irq_prescale;  /* Sunsoft-3 counter write order; RAMBO-1 CPU cycles / 4 */
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
    /* Famicom Disk System (loaded as mapper 20, runtime/fds.c): the BIOS is
     * prg, 32 KB RAM at $6000-$DFFF is prg_ram, 8 KB CHR RAM */
    uint8_t *disk;                   /* the sides as on the disk, FDS_RAW_SIDE bytes each */
    uint32_t disk_len[FDS_MAX_SIDES];
    uint32_t disk_hash;              /* of the image as loaded (save states check it) */
    uint8_t disk_sides, disk_dirty;  /* dirty: the game wrote to the disk */
    int8_t disk_side, disk_next;     /* inserted side (-1 none); side to insert at disk_insert_at */
    uint64_t disk_insert_at;         /* CPU cycle */
    uint32_t disk_pos, disk_delay;   /* head position; cycles to the next byte */
    uint16_t fds_reload, fds_counter, fds_crc;
    uint8_t fds_timer_on, fds_repeat, fds_timer_irq, fds_disk_irq;
    uint8_t fds_io;                  /* $4023: bit 0 disk, bit 1 sound registers enabled */
    uint8_t fds_ctrl, fds_motor;     /* $4025; the motor (stops by itself at the end of the disk) */
    uint8_t fds_wdata, fds_rdata, fds_ext;
    uint8_t fds_transfer, fds_gap_ended, fds_scanning, fds_end_of_head, fds_prev_crc, fds_bad_crc;
    /* automatic disk swaps (cart_fds_auto): $4032 polls in a row, the last
     * one, when the head last reached the end of the disk (0: reading) */
    uint8_t fds_auto, fds_auto_off;  /* off: two sides match one request (unlicensed disks) */
    uint16_t fds_polls;
    uint64_t fds_last_poll, fds_end_at;
} Cart;

/* Famicom Disk System images: .fds, with or without the 16-byte "FDS" header
 * (65500 bytes a side). cart_load needs the BIOS (8 KB, disksys.rom) for
 * them: cart_fds_bios keeps a pointer to it for later loads. */
int cart_is_fds(const uint8_t *data, size_t size);
void cart_fds_bios(const uint8_t *bios);
/* Ejects the disk now; `side` (>= 0) goes in at CPU cycle `at` (a real
 * swap takes a moment: games check that the disk was out). */
void cart_fds_insert(Cart *c, int side, uint64_t at);
/* Automatic swaps (on by default): when a game that has read the disk keeps
 * polling the drive ($4032), the disk comes out and a side goes back in; the
 * BIOS's disk check ($E445) then gets the side whose header matches the ID
 * the game asks for (`id`: 10 bytes, $FF = any). After Mesen2's FDS
 * auto-insert. */
void cart_fds_check(Cart *c, const uint8_t id[10]);

/* Returns 0 on success, else writes a message to err. Copies the data. */
int cart_load(Cart *c, const uint8_t *data, size_t size, char *err, size_t errlen);
void cart_free(Cart *c);

/* CPU $4020-$FFFF. open_bus is returned for unmapped addresses. */
uint8_t cart_cpu_read(Cart *c, uint16_t a, uint8_t open_bus);
/* Is a CPU write to `a` a mapper register? (The bus catches the PPU up first:
 * bank switches can be raster effects.) Mapper 79 decodes $4100 with A8 set
 * ($4100-$5FFF), mapper 87 $6000-$7FFF; the rest $8000-$FFFF. */
static inline int cart_is_reg(const Cart *c, uint16_t a) {
    if (c->mapper == 20) return a == 0x4025;  /* FDS: mirroring; $8000-$DFFF is RAM */
    if (a >= 0x8000) return c->mapper != 0;
    if (c->mapper == 79 || c->mapper == 113) return (a & 0xE100) == 0x4100;
    if (c->mapper == 34) return c->submapper == 1 && a >= 0x7FFD;  /* NINA-001 */
    if (c->mapper == 86) return a >= 0x6000 && a < 0x7000;
    if (c->mapper == 41) return a >= 0x6000 && a < 0x6800;
    if (c->mapper == 82) return (a & 0xFFF0) == 0x7EF0;
    if (c->mapper == 37 || c->mapper == 47 || c->mapper == 140 || c->mapper == 184) return a >= 0x6000;
    if (c->mapper == 5) return a >= 0x5000 && a < 0x6000;
    if (c->mapper == 19) return a >= 0x4800 && a < 0x6000;
    if (c->mapper == 16 && c->submapper != 5) return a >= 0x6000;  /* FCG-1/2 */
    if (c->mapper == 80 || c->mapper == 207) return (a & 0xFF70) == 0x7E70;  /* X1-005: $7EF0-$7EFF, A7 ignored */
    return c->mapper == 87 && a >= 0x6000;
}
/* $6000-$7FFF is no memory: a register or an EEPROM line (reads and writes
 * take the bus's slow path). */
static inline int cart_6000_is_io(const Cart *c) {
    switch (c->mapper) {
    case 16: case 34: case 37: case 41: case 47: case 80: case 82: case 86: case 87: case 140: case 157:
    case 159: case 184: case 188: case 207: return 1;
    }
    return 0;
}
/* VRC6 (24, 26): the register an address hits, as $x000-$x003 (26 swaps A0 and A1). */
static inline uint16_t cart_vrc6_reg(const Cart *c, uint16_t a) {
    if (c->mapper == 26) a = (uint16_t)((a & ~3) | (a & 1) << 1 | (a >> 1 & 1));
    return a & 0xF003;
}
/* VRC7 (85): the address line that picks the second register of a pair:
 * A3 (VRC7b, submapper 1), A4 (VRC7a, submapper 2), either when unknown. */
static inline int cart_vrc7_line(const Cart *c, uint16_t a) {
    return (a & (c->submapper == 1 ? 0x08 : c->submapper == 2 ? 0x10 : 0x18)) != 0;
}
/* MMC3's IRQ counter: MMC3, TxSROM (an MMC3 board wiring CHR bank bit 7 to
 * the nametables), Taito TC0690 (48), the MMC3 multicarts and TQROM (37, 47,
 * 119), Tengen RAMBO-1 (64, 158; in its A12 mode) */
static inline int cart_is_mmc3(const Cart *c) {
    switch (c->mapper) {
    case 4: case 37: case 47: case 48: case 64: case 118: case 119: case 158: return 1;
    }
    return 0;
}
/* A CPU read the PPU must be caught up for first (MMC5 scanline IRQ status). */
static inline int cart_read_syncs(const Cart *c, uint16_t a) { return c->mapper == 5 && a == 0x5204; }
/* Returns 1 if the PRG mapping may have changed (the bus re-maps its pages). */
int cart_cpu_write(Cart *c, uint16_t a, uint8_t v, uint64_t cpu_cycle);
/* PPU $0000-$1FFF (inline: the PPU fetches every few dots) */
static inline uint8_t cart_chr_read(Cart *c, uint16_t a) { return c->chr[c->chr_off[(a >> 10) & 7] + (a & 0x3FF)]; }
void cart_chr_write(Cart *c, uint16_t a, uint8_t v);
/* MMC5, Namco 163, TxSROM, X1-005 207, Sunsoft-4 (68), Namco 3425 (95) and
 * RAMBO-1 158 map each 1 KB nametable themselves: cart_nt_read/write (ciram
 * = the PPU's nametable RAM). The rest use cart_nt_index. */
static inline int cart_custom_nt(const Cart *c) {
    switch (c->mapper) {
    case 5: case 19: case 68: case 95: case 118: case 158: case 207: return 1;
    }
    return 0;
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
