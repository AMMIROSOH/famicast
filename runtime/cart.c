/* iNES parsing: nesdev "INES", "NES 2.0" (nesrecomp uses this file too).
 * Mapper behaviour: nesdev wiki "MMC1", "UxROM", "CNROM", "MMC3", "AxROM",
 * "Color Dreams", "GxROM", "INES Mapper 071", "NINA-003-006" (79),
 * "INES Mapper 087", "INES Mapper 185", "Namco 108" (206). */
#include "cart.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* MMC3 counts an A12 rise only after A12 was low for about 3 CPU cycles
 * (nesdev "MMC3": M2 filter); in PPU dots. Checked by mmc3_test_2. */
enum { A12_FILTER_DOTS = 10 };

static void set_prg16(Cart *c, int window, uint32_t bank16) {  /* window 0: $8000, 1: $C000 */
    uint32_t off = (bank16 * 0x4000) % c->prg_size;
    c->prg_off[window * 2] = off;
    c->prg_off[window * 2 + 1] = off + 0x2000;
}

static void set_prg32(Cart *c, uint32_t bank32) {
    uint32_t off = (bank32 * 0x8000) % c->prg_size;
    for (int i = 0; i < 4; i++) c->prg_off[i] = (off + i * 0x2000) % c->prg_size;
}

static void set_chr(Cart *c, int page, int count, uint32_t bank_1k) {
    for (int i = 0; i < count; i++) c->chr_off[page + i] = ((bank_1k + i) * 0x400) % c->chr_size;
}

static void mmc1_update(Cart *c) {
    uint8_t ctl = c->reg[0];
    if (!c->four_screen) {
        static const int MIRROR[4] = {MIRROR_SINGLE_LOW, MIRROR_SINGLE_HIGH, MIRROR_VERTICAL, MIRROR_HORIZONTAL};
        c->mirroring = MIRROR[ctl & 3];
    }
    /* SUROM (512 KB): CHR register bit 4 selects the 256 KB half */
    uint32_t outer = (c->prg_size == 0x80000 && (c->reg[1] & 0x10)) ? 16 : 0;  /* in 16 KB banks */
    uint32_t bank = c->reg[3] & 0x0F;
    uint32_t last = (c->prg_size > 0x40000 ? 0x40000 : c->prg_size) / 0x4000 - 1;
    switch ((ctl >> 2) & 3) {
    case 0:
    case 1: set_prg32(c, (outer + (bank & 0x0E)) / 2); break;
    case 2: set_prg16(c, 0, outer); set_prg16(c, 1, outer + bank); break;
    case 3: set_prg16(c, 0, outer + bank); set_prg16(c, 1, outer + last); break;
    }
    if (ctl & 0x10) {
        set_chr(c, 0, 4, (uint32_t)c->reg[1] * 4);
        set_chr(c, 4, 4, (uint32_t)c->reg[2] * 4);
    } else {
        set_chr(c, 0, 8, (uint32_t)(c->reg[1] & 0x1E) * 4);
    }
}

static void mmc3_update(Cart *c) {
    uint32_t n8 = c->prg_size / 0x2000;
    uint32_t r6 = c->reg[6] % n8, r7 = c->reg[7] % n8;
    uint32_t banks[4] = {r6, r7, n8 - 2, n8 - 1};
    if (c->mmc3_select & 0x40) { banks[0] = n8 - 2; banks[2] = r6; }
    for (int i = 0; i < 4; i++) c->prg_off[i] = banks[i] * 0x2000;
    int inv = (c->mmc3_select & 0x80) ? 4 : 0;  /* CHR A12 inversion swaps the halves */
    set_chr(c, 0 ^ inv, 2, c->reg[0] & 0xFE);
    set_chr(c, 2 ^ inv, 2, c->reg[1] & 0xFE);
    set_chr(c, 4 ^ inv, 1, c->reg[2]);
    set_chr(c, 5 ^ inv, 1, c->reg[3]);
    set_chr(c, 6 ^ inv, 1, c->reg[4]);
    set_chr(c, 7 ^ inv, 1, c->reg[5]);
}

/* Mapper 185: CHR reads only work while the latch matches the board's diodes
 * (copy protection: games check that they fail). reg[0] = NES 2.0 submapper. */
static int chr_enabled_185(const Cart *c) {
    if (c->reg[0] >= 4) return (c->latch & 3) == c->reg[0] - 4;
    return (c->latch & 0x0F) && c->latch != 0x13;  /* old iNES header: usual emulator guess */
}

static void update_banks(Cart *c) {
    uint32_t n16 = c->prg_size / 0x4000;
    switch (c->mapper) {
    case 0: set_prg16(c, 0, 0); set_prg16(c, 1, n16 - 1); set_chr(c, 0, 8, 0); break;
    case 1: mmc1_update(c); break;
    case 2: set_prg16(c, 0, c->latch); set_prg16(c, 1, n16 - 1); set_chr(c, 0, 8, 0); break;
    case 3: set_prg16(c, 0, 0); set_prg16(c, 1, n16 - 1); set_chr(c, 0, 8, (uint32_t)c->latch * 8); break;
    case 4: mmc3_update(c); break;
    case 7:
        set_prg32(c, c->latch & 7);
        set_chr(c, 0, 8, 0);
        c->mirroring = (c->latch & 0x10) ? MIRROR_SINGLE_HIGH : MIRROR_SINGLE_LOW;
        break;
    case 11: set_prg32(c, c->latch & 3); set_chr(c, 0, 8, (uint32_t)(c->latch >> 4) * 8); break;
    case 66: set_prg32(c, (c->latch >> 4) & 3); set_chr(c, 0, 8, (uint32_t)(c->latch & 3) * 8); break;
    case 71: set_prg16(c, 0, c->latch & 0x0F); set_prg16(c, 1, n16 - 1); set_chr(c, 0, 8, 0); break;
    case 79: set_prg32(c, (c->latch >> 3) & 1); set_chr(c, 0, 8, (uint32_t)(c->latch & 7) * 8); break;
    case 87:  /* CHR bank bits are swapped on the board */
        set_prg16(c, 0, 0); set_prg16(c, 1, n16 - 1);
        set_chr(c, 0, 8, (uint32_t)(((c->latch & 1) << 1) | ((c->latch >> 1) & 1)) * 8);
        break;
    case 185:
        set_prg16(c, 0, 0); set_prg16(c, 1, n16 - 1);
        set_chr(c, 0, 8, 0);
        if (!chr_enabled_185(c))
            for (int i = 0; i < 8; i++) c->chr_off[i] = c->chr_size;  /* the $FF page after CHR */
        break;
    case 206: mmc3_update(c); break;  /* Namco 108: MMC3 without modes, mirroring or IRQ */
    }
}

int cart_load(Cart *c, const uint8_t *d, size_t size, char *err, size_t errlen) {
    memset(c, 0, sizeof *c);
    if (size < 16 || memcmp(d, "NES\x1a", 4) != 0) {
        snprintf(err, errlen, "not an iNES file");
        return -1;
    }
    /* Old dumps have junk ("DiskDude!") in bytes 7-15; if bytes 12-15 are not
     * zero and it is not NES 2.0, byte 7 is junk too (nesdev "INES"). */
    int nes2 = (d[7] & 0x0C) == 0x08;
    int junk = !nes2 && (d[12] | d[13] | d[14] | d[15]);
    /* NES 2.0: mapper bits 8-11 in byte 8 (else mapper 515 would load as 3) */
    c->mapper = (d[6] >> 4) | (junk ? 0 : (d[7] & 0xF0)) | (nes2 ? (d[8] & 0x0F) << 8 : 0);
    c->four_screen = (d[6] & 0x08) != 0;
    c->mirroring = c->four_screen ? MIRROR_FOUR : (d[6] & 0x01) ? MIRROR_VERTICAL : MIRROR_HORIZONTAL;
    c->battery = (d[6] & 0x02) != 0;
    c->prg_size = d[4] * 0x4000u;
    c->chr_size = d[5] * 0x2000u;
    size_t off = 16 + ((d[6] & 0x04) ? 512 : 0);
    if (c->prg_size == 0 || size < off + c->prg_size + c->chr_size) {
        snprintf(err, errlen, "file shorter than its header says");
        return -1;
    }
    static const int SUPPORTED[] = {0, 1, 2, 3, 4, 7, 11, 66, 71, 79, 87, 185, 206};
    int ok = 0;
    for (size_t i = 0; i < sizeof SUPPORTED / sizeof *SUPPORTED; i++) ok |= c->mapper == SUPPORTED[i];
    if (!ok) {
        snprintf(err, errlen, "mapper %d not supported yet", c->mapper);
        return -1;
    }
    c->chr_ram = c->chr_size == 0;
    if (c->chr_ram) c->chr_size = 0x2000;
    c->prg = malloc(c->prg_size);
    c->chr = malloc(c->chr_size + 0x400);  /* + a page of $FF: CHR disabled (mapper 185) */
    memset(c->chr, 0, c->chr_size);
    memset(c->chr + c->chr_size, 0xFF, 0x400);
    memcpy(c->prg, d + off, c->prg_size);
    if (!c->chr_ram) memcpy(c->chr, d + off + c->prg_size, c->chr_size);
    if (c->mapper == 1) c->reg[0] = 0x0C;          /* power-on: last bank fixed at $C000 */
    if (c->mapper == 4 || c->mapper == 206) { c->reg[6] = 0; c->reg[7] = 1; }
    if (c->mapper == 185) c->reg[0] = nes2 ? d[8] >> 4 : 0;
    update_banks(c);
    return 0;
}

void cart_free(Cart *c) {
    free(c->prg);
    free(c->chr);
    c->prg = c->chr = NULL;
}

uint8_t cart_cpu_read(Cart *c, uint16_t a, uint8_t open_bus) {
    if (a >= 0x8000) return c->prg[c->prg_off[(a >> 13) & 3] + (a & 0x1FFF)];
    if (a >= 0x6000) return c->prg_ram[a - 0x6000];
    return open_bus;
}

static void mmc1_write(Cart *c, uint16_t a, uint8_t v, uint64_t cycle) {
    uint64_t last = c->mmc1_last_write;
    c->mmc1_last_write = cycle;
    if (v & 0x80) {
        c->mmc1_shift = c->mmc1_count = 0;
        c->reg[0] |= 0x0C;
        mmc1_update(c);
        return;
    }
    if (cycle == last + 1) return;  /* RMW double writes: only the first counts */
    c->mmc1_shift |= (uint8_t)((v & 1) << c->mmc1_count);
    if (++c->mmc1_count == 5) {
        c->reg[(a >> 13) & 3] = c->mmc1_shift;
        c->mmc1_shift = c->mmc1_count = 0;
        mmc1_update(c);
    }
}

static void mmc3_write(Cart *c, uint16_t a, uint8_t v) {
    int odd = a & 1;
    switch ((a >> 13) & 3) {
    case 0:
        if (!odd) c->mmc3_select = v;
        else c->reg[c->mmc3_select & 7] = v;
        mmc3_update(c);
        break;
    case 1:
        if (!odd && !c->four_screen) c->mirroring = (v & 1) ? MIRROR_HORIZONTAL : MIRROR_VERTICAL;
        break;  /* odd: PRG RAM protect, ignored */
    case 2:
        if (!odd) c->irq_latch = v;
        else { c->irq_counter = 0; c->irq_reload = 1; }
        break;
    case 3:
        if (!odd) { c->irq_enabled = 0; c->irq_line = 0; }
        else c->irq_enabled = 1;
        break;
    }
}

int cart_cpu_write(Cart *c, uint16_t a, uint8_t v, uint64_t cycle) {
    if (a < 0x8000 && cart_is_reg(c, a)) {  /* 79: $4100 (A8 set), 87: $6000-$7FFF */
        c->latch = v;
        update_banks(c);
        return c->mapper == 79;  /* 87 switches CHR only */
    }
    if (a < 0x6000) return 0;
    if (a < 0x8000) { c->prg_ram[a - 0x6000] = v; return 0; }
    switch (c->mapper) {
    case 0: case 79: case 87: return 0;
    case 1: mmc1_write(c, a, v, cycle); return 1;
    case 4: mmc3_write(c, a, v); return 1;
    case 206:  /* bank select and data only; 3 select bits */
        if (a < 0xA000) mmc3_write(c, a, (a & 1) ? v : v & 7);
        return 1;
    case 71:  /* $C000: PRG; $9000: single-screen select (BF9097 boards) */
        if (a >= 0xC000) { c->latch = v; update_banks(c); }
        else if (a >= 0x9000 && a < 0xA000) c->mirroring = (v & 0x10) ? MIRROR_SINGLE_HIGH : MIRROR_SINGLE_LOW;
        return 1;
    default: c->latch = v; update_banks(c); return 1;  /* simple latch mappers (bus conflicts ignored) */
    }
}

void cart_chr_write(Cart *c, uint16_t a, uint8_t v) {
    if (c->chr_ram) c->chr[c->chr_off[(a >> 10) & 7] + (a & 0x3FF)] = v;
}

void cart_ppu_address(Cart *c, uint16_t a, uint64_t dot) {
    if (c->mapper != 4) return;
    uint8_t a12 = (a >> 12) & 1;
    if (a12 && !c->a12) {
        if (dot - c->a12_low_since >= A12_FILTER_DOTS) {
            if (c->irq_counter == 0 || c->irq_reload) {
                c->irq_counter = c->irq_latch;
                c->irq_reload = 0;
            } else {
                c->irq_counter--;
            }
            if (c->irq_counter == 0 && c->irq_enabled) c->irq_line = 1;
        }
    } else if (!a12 && c->a12) {
        c->a12_low_since = dot;
    }
    c->a12 = a12;
}

int cart_irq_armed(const Cart *c) {
    return c->mapper == 4 && c->irq_enabled && (c->irq_counter <= 1 || c->irq_reload);
}
