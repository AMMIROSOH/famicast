/* iNES parsing: nesdev "INES", "NES 2.0" (nesrecomp uses this file too).
 * Mapper behaviour: nesdev wiki "MMC1", "UxROM", "CNROM", "MMC3", "AxROM",
 * "Color Dreams", "GxROM", "INES Mapper 071", "NINA-003-006" (79),
 * "INES Mapper 087", "INES Mapper 185", "Namco 108" (206), "VRC2 and VRC4",
 * "VRC IRQ" (21, 22, 23, 25), "MMC5" (5), "Namco 163" (19). */
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
 * (copy protection: games check that they fail). */
static int chr_enabled_185(const Cart *c) {
    if (c->submapper >= 4) return (c->latch & 3) == c->submapper - 4;
    return (c->latch & 0x0F) && c->latch != 0x13;  /* old iNES header: usual emulator guess */
}

/* ---- Konami VRC2/VRC4 (21, 22, 23, 25): one chip, boards wire its two
 * register lines A0/A1 to different CPU address lines. The NES 2.0 submapper
 * names the board; without it both wirings of the mapper are decoded at once
 * (they do not overlap). */
static int is_vrc(const Cart *c) { return c->mapper == 21 || c->mapper == 22 || c->mapper == 23 || c->mapper == 25; }

static int vrc_lines(const Cart *c, uint16_t a) {
    int s = c->submapper, a0, a1;
    switch (c->mapper) {
    case 21: /* 1 VRC4a: A1, A2; 2 VRC4c: A6, A7 */
        a0 = s == 1 ? a >> 1 : s == 2 ? a >> 6 : (a >> 1) | (a >> 6);
        a1 = s == 1 ? a >> 2 : s == 2 ? a >> 7 : (a >> 2) | (a >> 7);
        break;
    case 22: a0 = a >> 1; a1 = a; break;  /* VRC2a: A1, A0 */
    case 23: /* 1 VRC4f, 3 VRC2b: A0, A1; 2 VRC4e: A2, A3 */
        a0 = s == 1 || s == 3 ? a : s == 2 ? a >> 2 : a | (a >> 2);
        a1 = s == 1 || s == 3 ? a >> 1 : s == 2 ? a >> 3 : (a >> 1) | (a >> 3);
        break;
    default: /* 25: 1 VRC4b, 3 VRC2c: A1, A0; 2 VRC4d: A3, A2 */
        a0 = s == 1 || s == 3 ? a >> 1 : s == 2 ? a >> 3 : (a >> 1) | (a >> 3);
        a1 = s == 1 || s == 3 ? a : s == 2 ? a >> 2 : a | (a >> 2);
        break;
    }
    return (a0 & 1) | (a1 & 1) << 1;
}

static void vrc_update(Cart *c) {
    uint32_t n8 = c->prg_size / 0x2000;
    uint32_t banks[4] = {c->vrc_prg[0] % n8, c->vrc_prg[1] % n8, n8 - 2, n8 - 1};
    if (c->vrc_swap) { banks[2] = banks[0]; banks[0] = n8 - 2; }
    for (int i = 0; i < 4; i++) c->prg_off[i] = banks[i] * 0x2000;
    for (int i = 0; i < 8; i++)  /* VRC2a (22) has CHR A10 unconnected: bank >> 1 */
        set_chr(c, i, 1, c->mapper == 22 ? c->vrc_chr[i] >> 1 : c->vrc_chr[i]);
}

static void vrc_write(Cart *c, uint16_t a, uint8_t v) {
    int line = vrc_lines(c, a);
    switch (a >> 12) {
    case 0x8: c->vrc_prg[0] = v & 0x1F; break;
    case 0xA: c->vrc_prg[1] = v & 0x1F; break;
    case 0x9:
        if (c->vrc2 || line < 2) {  /* mirroring (VRC2: one bit) */
            static const int MIRROR[4] = {MIRROR_VERTICAL, MIRROR_HORIZONTAL, MIRROR_SINGLE_LOW, MIRROR_SINGLE_HIGH};
            if (!c->four_screen) c->mirroring = MIRROR[c->vrc2 ? v & 1 : v & 3];
        } else {
            c->vrc_swap = (v >> 1) & 1;  /* bit 0: PRG RAM enable, ignored */
        }
        break;
    case 0xB: case 0xC: case 0xD: case 0xE: {  /* CHR: low nibble, then high bits */
        int page = ((a >> 12) - 0xB) * 2 + (line >> 1);
        if (line & 1) c->vrc_chr[page] = (uint16_t)((c->vrc_chr[page] & 0x0F) | (v & 0x1F) << 4);
        else c->vrc_chr[page] = (uint16_t)((c->vrc_chr[page] & 0x1F0) | (v & 0x0F));
        break;
    }
    case 0xF:  /* VRC4 IRQ: latch low, latch high, control, acknowledge */
        if (c->vrc2) break;
        if (line == 0) c->irq_latch = (uint8_t)((c->irq_latch & 0xF0) | (v & 0x0F));
        else if (line == 1) c->irq_latch = (uint8_t)((c->irq_latch & 0x0F) | v << 4);
        else if (line == 2) {
            c->vrc_irq_ctrl = v & 7;
            c->irq_line = 0;
            if (v & 2) { c->irq_counter = c->irq_latch; c->vrc_prescaler = 341; }
        } else {
            c->irq_line = 0;
            c->vrc_irq_ctrl = (uint8_t)((c->vrc_irq_ctrl & ~2) | (c->vrc_irq_ctrl & 1) << 1);
        }
        break;
    }
    vrc_update(c);
}

/* VRC IRQ: while enabled the counter is clocked every CPU cycle (M set) or
 * every 113 2/3 (scanline mode: the prescaler drops by 3 per cycle and adds
 * 341 when it reaches 0 or less). Clocking $FF reloads the latch and asserts
 * the IRQ. Runs in closed form: the bus only calls it on its slow path. */
void cart_run(Cart *c, uint64_t cycle) {
    if (cycle <= c->irq_cycle) { c->irq_cycle = cycle; return; }
    int64_t n = (int64_t)(cycle - c->irq_cycle);
    c->irq_cycle = cycle;
    if (c->mapper == 19) {  /* Namco 163: counts up to $7FFF, then IRQ and stop */
        if (!c->n163_irq_enable || c->n163_counter >= 0x7FFF) return;
        int64_t to = c->n163_counter + n;
        if (to >= 0x7FFF) { to = 0x7FFF; c->irq_line = 1; }
        c->n163_counter = (uint16_t)to;
        return;
    }
    if (!is_vrc(c) || !(c->vrc_irq_ctrl & 2)) return;
    int64_t k = n;  /* counter clocks in these n cycles */
    if (!(c->vrc_irq_ctrl & 4)) {
        k = 3 * n < c->vrc_prescaler ? 0 : (3 * n - c->vrc_prescaler) / 341 + 1;
        c->vrc_prescaler = (int16_t)(c->vrc_prescaler - 3 * n + 341 * k);
    }
    int64_t first = 0x100 - c->irq_counter;  /* clocks until $FF is clocked */
    if (k < first) { c->irq_counter = (uint8_t)(c->irq_counter + k); return; }
    c->irq_line = 1;
    c->irq_counter = (uint8_t)(c->irq_latch + (k - first) % (0x100 - c->irq_latch));
}

int64_t cart_cycles_to_irq(const Cart *c, uint64_t cycle) {
    int64_t need;
    if (c->mapper == 19) {
        if (!c->n163_irq_enable || c->n163_counter >= 0x7FFF) return -1;
        need = 0x7FFF - c->n163_counter;
    } else {
        if (!is_vrc(c) || !(c->vrc_irq_ctrl & 2)) return -1;
        int64_t m = 0x100 - c->irq_counter;  /* clocks to go */
        need = (c->vrc_irq_ctrl & 4) ? m : (c->vrc_prescaler + 341 * (m - 1) + 2) / 3;
    }
    int64_t left = need - (int64_t)(cycle - c->irq_cycle);
    return left > 0 ? left : 0;
}

/* ---- MMC5 (5) ----
 * PRG: $5100 mode 0-3 = one 32 KB, two 16 KB, 16+8+8 KB or four 8 KB banks
 * from $5114-$5117 (8 KB units; bit 7 clear on $5114-$5116 = PRG RAM there);
 * $5113 banks the PRG RAM at $6000. Audio, the vertical split ($5200-$5202)
 * and PRG RAM write protection ($5102/$5103) are not emulated. */

/* PRG RAM bank -> offset: bit 2 selects the chip, bits 0-1 the 8 KB bank in
 * it. 16 KB boards are two 8 KB chips (banks 0 and 4 differ, 0 and 1 do not);
 * 8 and 32 KB are one chip at select 0 (banks 4-7, open bus there, mirror). */
static uint32_t mmc5_ram_off(const Cart *c, uint32_t bank) {
    uint32_t chip = c->prg_ram_size == 0x4000 ? 0x2000 : (c->prg_ram_size < 0x8000 ? c->prg_ram_size : 0x8000);
    return (((bank >> 2) & 1) * chip + ((bank & 3) * 0x2000) % chip) % c->prg_ram_size;
}

static void mmc5_update(Cart *c) {
    uint32_t n8 = c->prg_size / 0x2000, b[4];
    const uint8_t *r = c->mmc5_prg;
    uint8_t reg[4];  /* the register each window comes from ($5114-$5117 = 1-4) */
    switch (c->mmc5_prg_mode) {
    case 0: for (int w = 0; w < 4; w++) { b[w] = (r[4] & 0x7Cu) + (uint32_t)w; reg[w] = 4; } break;
    case 1:
        b[0] = r[2] & 0x7Eu; b[1] = b[0] + 1; b[2] = r[4] & 0x7Eu; b[3] = b[2] + 1;
        reg[0] = reg[1] = 2; reg[2] = reg[3] = 4;
        break;
    case 2:
        b[0] = r[2] & 0x7Eu; b[1] = b[0] + 1; b[2] = r[3] & 0x7Fu; b[3] = r[4] & 0x7Fu;
        reg[0] = reg[1] = 2; reg[2] = 3; reg[3] = 4;
        break;
    default: for (int w = 0; w < 4; w++) { b[w] = r[w + 1] & 0x7Fu; reg[w] = (uint8_t)(w + 1); } break;
    }
    c->prg_ram_window = 0;
    for (int w = 0; w < 4; w++) {
        if (reg[w] < 4 && !(r[reg[w]] & 0x80)) {  /* PRG RAM ($5117 is always ROM) */
            c->prg_ram_window |= (uint8_t)(1 << w);
            c->prg_off[w] = mmc5_ram_off(c, b[w]);
        } else {
            c->prg_off[w] = (b[w] % n8) * 0x2000;
        }
    }
    c->ram_off = mmc5_ram_off(c, r[0]);
    /* CHR: $5101 mode = 8, 4, 2 or 1 KB banks. Set A for sprites, set B (4 KB,
     * repeated) for the background with 8x16 sprites; with 8x8 sprites the
     * set written last serves both. */
    uint32_t size = 8u >> c->mmc5_chr_mode;  /* 1 KB pages per bank */
    int sp_b = !c->mmc5_sprite16 && c->mmc5_last_b, bg_b = c->mmc5_sprite16 || c->mmc5_last_b;
    for (uint32_t i = 0; i < 8; i++) {
        uint32_t a = c->mmc5_chr[i | (size - 1)] * size + (i & (size - 1));
        uint32_t bb = c->mmc5_chr[8 + ((i & 3) | ((size - 1) & 3))] * size + (i & (size - 1));
        c->chr_off[i] = ((sp_b ? bb : a) * 0x400) % c->chr_size;
        c->chr_off_bg[i] = ((bg_b ? bb : a) * 0x400) % c->chr_size;
    }
}

static int mmc5_write(Cart *c, uint16_t a, uint8_t v) {
    if (a >= 0x5C00) {  /* ExRAM: writable unless mode 3 (read-only) */
        if (c->mmc5_exram_mode != 3) c->exram[a - 0x5C00] = v;
        return 0;
    }
    switch (a) {
    case 0x5100: c->mmc5_prg_mode = v & 3; break;
    case 0x5101: c->mmc5_chr_mode = v & 3; break;
    case 0x5104: c->mmc5_exram_mode = v & 3; break;
    case 0x5105: c->mmc5_nt = v; break;
    case 0x5106: c->mmc5_fill_tile = v; break;
    case 0x5107: c->mmc5_fill_attr = (uint8_t)((v & 3) * 0x55); break;
    case 0x5113: case 0x5114: case 0x5115: case 0x5116: case 0x5117: c->mmc5_prg[a - 0x5113] = v; break;
    case 0x5130: c->mmc5_chr_upper = v & 3; break;
    case 0x5203: c->mmc5_irq_compare = v; break;
    case 0x5204: c->mmc5_irq_enable = v >> 7; c->irq_line = c->mmc5_irq_pending && c->mmc5_irq_enable; break;
    case 0x5205: case 0x5206: c->mmc5_mul[a - 0x5205] = v; break;
    default:
        if (a < 0x5120 || a > 0x512B) return 0;  /* audio, RAM protect, split: ignored */
        c->mmc5_chr[a - 0x5120] = (uint16_t)(v | c->mmc5_chr_upper << 8);
        c->mmc5_last_b = a >= 0x5128;
        break;
    }
    mmc5_update(c);
    return 1;
}

static uint8_t mmc5_read(Cart *c, uint16_t a, uint8_t open_bus) {
    if (a >= 0x5C00) return c->mmc5_exram_mode >= 2 ? c->exram[a - 0x5C00] : open_bus;
    unsigned product = (unsigned)c->mmc5_mul[0] * c->mmc5_mul[1];
    switch (a) {
    case 0x5204: {  /* IRQ pending, in frame; reading acknowledges */
        uint8_t s = (uint8_t)(c->mmc5_irq_pending << 7 | c->mmc5_in_frame << 6);
        c->mmc5_irq_pending = 0;
        c->irq_line = 0;
        return s;
    }
    case 0x5205: return (uint8_t)product;
    case 0x5206: return (uint8_t)(product >> 8);
    }
    return open_bus;
}

/* The scanline counter: the first rendered line sets "in frame" and counts
 * 0, each later one counts up; reaching $5203 sets the pending flag. */
void cart_mmc5_scanline(Cart *c, int line, int rendering) {
    if (!rendering || line >= 240) { c->mmc5_in_frame = 0; return; }
    if (!c->mmc5_in_frame) {
        c->mmc5_in_frame = 1;
        c->mmc5_counter = 0;
        c->mmc5_irq_pending = 0;
    } else if (++c->mmc5_counter == c->mmc5_irq_compare) {
        c->mmc5_irq_pending = 1;
    }
    c->irq_line = c->mmc5_irq_pending && c->mmc5_irq_enable;
}

void cart_mmc5_ppuctrl(Cart *c, uint8_t ctrl) {
    uint8_t s = (ctrl >> 5) & 1;
    if (s != c->mmc5_sprite16) { c->mmc5_sprite16 = s; mmc5_update(c); }
}

/* ExRAM mode 1: each tile's ExRAM byte gives its 4 KB CHR bank (bits 0-5)
 * and palette (bits 6-7). */
uint8_t cart_mmc5_bg_read(Cart *c, uint16_t a, uint16_t v) {
    if (c->mmc5_exram_mode == 1) {
        uint32_t bank = (c->exram[v & 0x3FF] & 0x3Fu) | (uint32_t)c->mmc5_chr_upper << 6;
        return c->chr[(bank * 0x1000 + (a & 0x0FFF)) % c->chr_size];
    }
    return c->chr[c->chr_off_bg[(a >> 10) & 7] + (a & 0x3FF)];
}

int cart_mmc5_attr(const Cart *c, uint16_t v, int at_bits) {
    return c->mmc5_exram_mode == 1 ? c->exram[v & 0x3FF] >> 6 : at_bits;
}

/* ---- Namco 163 (19): 1 KB CHR banks at $8000-$B800, nametables at
 * $C000-$D800 ($E0-$FF: nametable RAM page, else a CHR ROM page), 8 KB PRG
 * at $E000/$E800/$F000 with $E000 fixed to the last bank, a 15-bit cycle
 * IRQ at $5000/$5800, 128 bytes of RAM behind $4800 (address at $F800).
 * shortcut: with $E800 bit 6/7 clear, CHR values $E0-$FF select nametable
 * RAM as pattern data; read as CHR ROM here (the 21 games checked either set
 * both bits or never write such values, docs/06). The wavetable sound is
 * not emulated. */
static void n163_update(Cart *c) {
    uint32_t n8 = c->prg_size / 0x2000;
    for (int w = 0; w < 3; w++) c->prg_off[w] = (c->n163_prg[w] % n8) * 0x2000;
    c->prg_off[3] = (n8 - 1) * 0x2000;
    for (int i = 0; i < 8; i++) set_chr(c, i, 1, c->n163_chr[i]);
}

static void n163_write(Cart *c, uint16_t a, uint8_t v) {
    if (a < 0x5000) {  /* $4800: data port */
        c->n163_ram[c->n163_addr & 0x7F] = v;
        if (c->n163_addr & 0x80) c->n163_addr = (uint8_t)(0x80 | ((c->n163_addr + 1) & 0x7F));
        return;
    }
    if (a < 0x6000) {  /* IRQ counter low / high + enable; either acknowledges */
        if (a < 0x5800) c->n163_counter = (uint16_t)((c->n163_counter & 0x7F00) | v);
        else { c->n163_counter = (uint16_t)((c->n163_counter & 0xFF) | (v & 0x7F) << 8); c->n163_irq_enable = v >> 7; }
        c->irq_line = 0;
        return;
    }
    if (a < 0x8000) return;  /* PRG RAM: written by cart_cpu_write */
    if (a < 0xC000) c->n163_chr[(a - 0x8000) >> 11] = v;
    else if (a < 0xE000) c->n163_nt[(a - 0xC000) >> 11] = v;
    else if (a < 0xF800) c->n163_prg[(a - 0xE000) >> 11] = v & 0x3F;
    else c->n163_addr = v;
    n163_update(c);
}

static uint8_t n163_read(Cart *c, uint16_t a, uint8_t open_bus) {
    if (a < 0x4800) return open_bus;
    if (a < 0x5000) {
        uint8_t v = c->n163_ram[c->n163_addr & 0x7F];
        if (c->n163_addr & 0x80) c->n163_addr = (uint8_t)(0x80 | ((c->n163_addr + 1) & 0x7F));
        return v;
    }
    if (a < 0x5800) return (uint8_t)c->n163_counter;
    return (uint8_t)(c->n163_counter >> 8 | c->n163_irq_enable << 7);
}

uint8_t cart_nt_read(Cart *c, const uint8_t *ciram, uint16_t a) {
    int q = (a >> 10) & 3, off = a & 0x3FF;
    if (c->mapper == 19) {
        uint8_t v = c->n163_nt[q];
        return v >= 0xE0 ? ciram[(v & 1) * 0x400 + off] : c->chr[((uint32_t)v * 0x400 + (uint32_t)off) % c->chr_size];
    }
    switch ((c->mmc5_nt >> (q * 2)) & 3) {  /* MMC5 $5105 */
    case 0: return ciram[off];
    case 1: return ciram[0x400 + off];
    case 2: return c->mmc5_exram_mode <= 1 ? c->exram[off] : 0;
    default: return off < 0x3C0 ? c->mmc5_fill_tile : c->mmc5_fill_attr;
    }
}

void cart_nt_write(Cart *c, uint8_t *ciram, uint16_t a, uint8_t v) {
    int q = (a >> 10) & 3, off = a & 0x3FF;
    if (c->mapper == 19) {
        if (c->n163_nt[q] >= 0xE0) ciram[(c->n163_nt[q] & 1) * 0x400 + off] = v;  /* CHR ROM: ignored */
        return;
    }
    switch ((c->mmc5_nt >> (q * 2)) & 3) {
    case 0: ciram[off] = v; break;
    case 1: ciram[0x400 + off] = v; break;
    case 2: if (c->mmc5_exram_mode <= 1) c->exram[off] = v; break;
    default: break;  /* fill mode */
    }
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
    case 21: case 22: case 23: case 25: vrc_update(c); break;
    case 5: mmc5_update(c); break;
    case 19: n163_update(c); break;
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
    c->submapper = nes2 ? d[8] >> 4 : 0;
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
    static const int SUPPORTED[] = {0, 1, 2, 3, 4, 5, 7, 11, 19, 21, 22, 23, 25, 66, 71, 79, 87, 185, 206};
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
    c->vrc2 = c->mapper == 22 || ((c->mapper == 23 || c->mapper == 25) && c->submapper == 3);
    c->prg_ram_size = 0x2000;
    if (c->mapper == 5) {
        /* NES 2.0 byte 10: PRG RAM and NVRAM sizes (64 << shift); else the most a board has */
        uint32_t ram = (d[10] & 15) ? 64u << (d[10] & 15) : 0, nv = (d[10] >> 4) ? 64u << (d[10] >> 4) : 0;
        uint32_t total = nes2 ? ram + nv : 0;
        c->prg_ram_size = total >= 0x2000 && total <= 0x10000 ? total : 0x10000;
        c->mmc5_prg_mode = 3;
        c->mmc5_prg[4] = 0xFF;  /* power-on: the last bank at $E000 (reset vector) */
    }
    update_banks(c);
    return 0;
}

void cart_free(Cart *c) {
    free(c->prg);
    free(c->chr);
    c->prg = c->chr = NULL;
}

uint8_t cart_cpu_read(Cart *c, uint16_t a, uint8_t open_bus) {
    if (a >= 0x8000) {
        int w = (a >> 13) & 3;
        return ((c->prg_ram_window >> w & 1) ? c->prg_ram : c->prg)[c->prg_off[w] + (a & 0x1FFF)];
    }
    if (a >= 0x6000) return c->prg_ram[c->ram_off + (a - 0x6000)];
    if (c->mapper == 5 && a >= 0x5000) return mmc5_read(c, a, open_bus);
    if (c->mapper == 19) return n163_read(c, a, open_bus);
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
    if (a < 0x8000 && cart_is_reg(c, a)) {  /* 79: $4100 (A8 set), 87: $6000-$7FFF, 5, 19: $5xxx */
        if (c->mapper == 5) return mmc5_write(c, a, v);
        if (c->mapper == 19) { n163_write(c, a, v); return 0; }
        c->latch = v;
        update_banks(c);
        return c->mapper == 79;  /* 87 switches CHR only */
    }
    if (a < 0x6000) return 0;
    if (a < 0x8000) { c->prg_ram[c->ram_off + (a - 0x6000)] = v; return 0; }
    if (c->prg_ram_window >> ((a >> 13) & 3) & 1) {  /* MMC5: PRG RAM in a ROM window */
        c->prg_ram[c->prg_off[(a >> 13) & 3] + (a & 0x1FFF)] = v;
        return 0;
    }
    switch (c->mapper) {
    case 0: case 5: case 79: case 87: return 0;
    case 19: n163_write(c, a, v); return 1;
    case 1: mmc1_write(c, a, v, cycle); return 1;
    case 4: mmc3_write(c, a, v); return 1;
    case 21: case 22: case 23: case 25: vrc_write(c, a, v); return 1;
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
