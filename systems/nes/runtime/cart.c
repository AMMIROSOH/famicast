/* iNES parsing: nesdev "INES", "NES 2.0" (nesrecomp uses this file too).
 * Mapper behaviour: nesdev wiki "MMC1", "UxROM", "CNROM", "MMC3", "AxROM",
 * "Color Dreams", "GxROM", "INES Mapper 071", "NINA-003-006" (79),
 * "INES Mapper 087", "INES Mapper 185", "Namco 108" (206), "VRC2 and VRC4",
 * "VRC IRQ" (21, 22, 23, 25), "MMC5" (5), "Namco 163" (19), "MMC2" (9),
 * "MMC4" (10), "INES Mapper 018" (Jaleco SS88006), "Sunsoft FME-7" (69),
 * "INES Mapper 210" (Namco 175/340), "Bandai FCG board", "INES Mapper 016",
 * "INES Mapper 153", "INES Mapper 157", "INES Mapper 159", "INES Mapper 118"
 * (TxSROM), "VRC1" (75), "VRC6" (24, 26), "VRC7" (85), "INES Mapper 032" (Irem G-101), "INES Mapper 033"
 * (Taito TC0190), "INES Mapper 048" (TC0690), "Taito X1-005" (80), "INES
 * Mapper 207" and "NES 2.0 submappers" (032: 1); the EEPROMs from
 * the 24C02/X24C01 datasheets' I2C protocol. */
#include "cart.h"
#include "fds.h"

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

static int is_rambo(const Cart *c) { return c->mapper == 64 || c->mapper == 158; }

/* MMC3 and the boards built on it or on the Namco 108 (206). Multicarts 37
 * and 47 add an outer bank register at $6000 (written while $A001 enables
 * PRG RAM): 37 bits 0-2 pick the PRG block (0-2: the first 64 KB, 3: the
 * second, 4-6: the 128 KB at $20000, 7: the 64 KB at $30000), bit 2 the
 * 128 KB CHR half; 47 bit 0 both 128 KB halves. TQROM (119): CHR bank bit 6
 * selects the 8 KB CHR RAM. RAMBO-1 (64, 158): R8/R9 1 KB banks at
 * $0400/$0C00 when $8000 bit 5 is set, RF a third switchable PRG bank. Namco
 * 3446 (76): R2-R5 are 2 KB banks. Namco 3433/3443 (88, 154): CHR A16 = PPU
 * A12. Namco 3425 (95): R0/R1 bit 5 drive CIRAM A10. */
static void mmc3_update(Cart *c) {
    uint32_t n8 = c->prg_size / 0x2000, pa = 0xFF, po = 0, ca = 0xFF, co = 0;
    if (c->mapper == 37) {
        uint8_t b = c->outer & 7;
        pa = b >= 4 && b < 7 ? 0x0F : 0x07;
        po = b < 3 ? 0 : b == 3 ? 0x08 : b < 7 ? 0x10 : 0x18;
        ca = 0x7F, co = (b & 4u) << 5;
    } else if (c->mapper == 47) {
        pa = 0x0F, po = (c->outer & 1u) << 4, ca = 0x7F, co = (c->outer & 1u) << 7;
    }
    uint32_t fix2 = pa == 0xFF ? n8 - 2 : (0xFEu & pa) | po, fix1 = pa == 0xFF ? n8 - 1 : (0xFFu & pa) | po;
    uint32_t r6 = ((c->reg[6] & pa) | po) % n8, r7 = ((c->reg[7] & pa) | po) % n8;
    uint32_t banks[4] = {r6, r7, fix2 % n8, fix1 % n8};
    if (is_rambo(c)) {  /* RF at $C000, or at $8000 with R6/R7 moved up */
        uint32_t rf = c->reg[15] % n8;
        if (c->mmc3_select & 0x40) banks[0] = rf, banks[1] = r6, banks[2] = r7;
        else banks[2] = rf;
    } else if (c->mmc3_select & 0x40) { banks[0] = fix2 % n8; banks[2] = r6; }
    for (int i = 0; i < 4; i++) c->prg_off[i] = banks[i] * 0x2000;
    int inv = (c->mmc3_select & 0x80) ? 4 : 0;  /* CHR A12 inversion swaps the halves */
    const uint8_t *r = c->reg;
    uint32_t k[8] = {r[0] & 0xFEu, r[0] | 1u, r[1] & 0xFEu, r[1] | 1u, r[2], r[3], r[4], r[5]};
    if (is_rambo(c) && (c->mmc3_select & 0x20)) k[0] = r[0], k[1] = r[8], k[2] = r[1], k[3] = r[9];
    for (int i = 0; i < 8 && c->mapper == 76; i++) k[i] = r[2 + i / 2] * 2u + (i & 1u);
    for (int i = 0; i < 8 && (c->mapper == 88 || c->mapper == 154); i++) k[i] = (k[i] & 0x3F) | (i < 4 ? 0 : 0x40);
    for (int i = 0; i < 8; i++) {
        uint32_t b = (k[i] & ca) | co;
        if (c->mapper == 119 && (b & 0x40))  /* TQROM: CHR RAM after the ROM */
            c->chr_off[i ^ inv] = c->chr_rom_size + (b & 7) * 0x400;
        else
            c->chr_off[i ^ inv] = (b * 0x400) % (c->chr_rom_size ? c->chr_rom_size : c->chr_size);
    }
    /* TxSROM: CHR bank bit 7 drives CIRAM A10, so nametable q follows the
     * register of CHR page q ($0000-$0FFF): R0/R1 (2 KB, two nametables
     * each), or R2-R5 with A12 inversion. The mirroring register does nothing.
     * RAMBO-1 158 the same, with its 1 KB banks; Namco 3425 (95) with bit 5. */
    if (c->mapper == 118 || c->mapper == 158)
        for (int q = 0; q < 4; q++) c->txsrom_nt[q] = (uint8_t)(k[inv ? 4 + q : q] >> 7);
    if (c->mapper == 95)
        for (int q = 0; q < 4; q++) c->txsrom_nt[q] = (r[q >> 1] >> 5) & 1;
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
static int is_vrc(const Cart *c) {  /* the VRC IRQ: VRC4 (and VRC2, which has none: vrc2), VRC6 */
    return c->mapper == 21 || c->mapper == 22 || c->mapper == 23 || c->mapper == 25 || c->mapper == 24 ||
           c->mapper == 26 || c->mapper == 85;
}

/* VRC IRQ control ($F001 style: M, E, A; acknowledges, reloads when E) and
 * acknowledge ($F002 style: copies A to E). */
static void vrc_irq_control(Cart *c, uint8_t v) {
    c->vrc_irq_ctrl = v & 7;
    c->irq_line = 0;
    if (v & 2) { c->irq_counter = c->irq_latch; c->vrc_prescaler = 341; }
}

static void vrc_irq_ack(Cart *c) {
    c->irq_line = 0;
    c->vrc_irq_ctrl = (uint8_t)((c->vrc_irq_ctrl & ~2) | (c->vrc_irq_ctrl & 1) << 1);
}

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
        else if (line == 2) vrc_irq_control(c, v);
        else vrc_irq_ack(c);
        break;
    }
    vrc_update(c);
}

/* ---- Konami VRC7 (85): pairs of registers told apart by one address line
 * (cart_vrc7_line). $8000/$8000+line, $9000: 8 KB PRG at $8000/$A000/$C000,
 * $E000 the last bank; $A000-$D000 (and +line) 1 KB CHR; $E000 bits 0-1
 * mirroring (vertical, horizontal, one screen A, B), $E000+line IRQ latch;
 * $F000 IRQ control, $F000+line acknowledge (the VRC IRQ). $9000+line: the
 * sound ports (runtime/apu.c), as is $E000 bit 6 (silence). shortcut: $E000
 * bit 7 (PRG RAM enable) is ignored. */
static void vrc7_update(Cart *c) {
    uint32_t n8 = c->prg_size / 0x2000;
    for (int w = 0; w < 3; w++) c->prg_off[w] = (c->prg_bank[w] % n8) * 0x2000;
    c->prg_off[3] = (n8 - 1) * 0x2000;
    for (int i = 0; i < 8; i++) set_chr(c, i, 1, c->chr_bank[i]);
}

static void vrc7_write(Cart *c, uint16_t a, uint8_t v) {
    int line = cart_vrc7_line(c, a);
    switch (a & 0xF000) {
    case 0x8000: c->prg_bank[line] = v & 0x3F; break;
    case 0x9000: if (!line) c->prg_bank[2] = v & 0x3F; break;
    case 0xA000: case 0xB000: case 0xC000: case 0xD000: c->chr_bank[((a >> 12) - 0xA) * 2 + line] = v; break;
    case 0xE000:
        if (line) c->irq_latch = v;
        else if (!c->four_screen) {
            static const int MIRROR[4] = {MIRROR_VERTICAL, MIRROR_HORIZONTAL, MIRROR_SINGLE_LOW, MIRROR_SINGLE_HIGH};
            c->mirroring = MIRROR[v & 3];
        }
        break;
    case 0xF000:
        if (line) vrc_irq_ack(c);
        else vrc_irq_control(c, v);
        break;
    }
    vrc7_update(c);
}

/* ---- Konami VRC6 (24; 26 swaps A0 and A1): $8000 16 KB PRG at $8000,
 * $C000 8 KB at $C000, $E000 the last bank; $D000-$D003 and $E000-$E003
 * 1 KB CHR R0-R7; $B003 bits 2-3 mirroring (vertical, horizontal, one
 * screen A, B); $F000 IRQ latch, $F001 control, $F002 acknowledge (the
 * VRC IRQ). Sound ($9000-$B002): runtime/apu.c. shortcut: only $B003's
 * banking mode 0 with CIRAM nametables, the one the three games use (they
 * write $20-$2C and $A0-$AC); modes 1-3 and nametables from CHR ROM are
 * not emulated, nor bit 7 (PRG RAM enable): upgrade for homebrew. */
static void vrc6_update(Cart *c) {
    uint32_t n8 = c->prg_size / 0x2000;
    c->prg_off[0] = ((c->prg_bank[0] * 2u) % n8) * 0x2000;
    c->prg_off[1] = ((c->prg_bank[0] * 2u + 1) % n8) * 0x2000;
    c->prg_off[2] = (c->prg_bank[1] % n8) * 0x2000;
    c->prg_off[3] = (n8 - 1) * 0x2000;
    for (int i = 0; i < 8; i++) set_chr(c, i, 1, c->chr_bank[i]);
}

static void vrc6_write(Cart *c, uint16_t a, uint8_t v) {
    uint16_t r = cart_vrc6_reg(c, a);
    switch (r & 0xF000) {
    case 0x8000: c->prg_bank[0] = v & 0x0F; break;
    case 0xC000: c->prg_bank[1] = v & 0x1F; break;
    case 0xD000: c->chr_bank[r & 3] = v; break;
    case 0xE000: c->chr_bank[4 + (r & 3)] = v; break;
    case 0xB000:
        if (r == 0xB003 && !c->four_screen) {
            static const int MIRROR[4] = {MIRROR_VERTICAL, MIRROR_HORIZONTAL, MIRROR_SINGLE_LOW, MIRROR_SINGLE_HIGH};
            c->mirroring = MIRROR[(v >> 2) & 3];
        }
        break;
    case 0xF000:
        if (r == 0xF000) c->irq_latch = v;
        else if (r == 0xF001) vrc_irq_control(c, v);
        else if (r == 0xF002) vrc_irq_ack(c);
        break;
    }
    vrc6_update(c);
}

/* SS88006 and FME-7: a 16-bit counter counts down every CPU cycle while
 * enabled; the IRQ asserts when it wraps (SS88006: only the low 4, 8 or 12
 * bits count, the rest keep their value) and stays until acknowledged.
 * Returns the counting bits (0: stopped); *irq: whether a wrap asserts it. */
static int is_bandai(const Cart *c) {
    return c->mapper == 16 || c->mapper == 153 || c->mapper == 157 || c->mapper == 159;
}

static uint16_t count16_mask(const Cart *c, int *irq) {
    uint8_t r = c->irq_ctrl;
    if (c->mapper == 69) { *irq = r & 1; return (r & 0x80) ? 0xFFFF : 0; }
    *irq = 1;
    if (is_bandai(c)) return (r & 1) ? 0xFFFF : 0;  /* IRQ at the clock that finds 0 (Mesen2) */
    if (c->mapper != 18 || !(r & 1)) return 0;
    return (r & 8) ? 0x000F : (r & 4) ? 0x00FF : (r & 2) ? 0x0FFF : 0xFFFF;
}

/* VRC IRQ: while enabled the counter is clocked every CPU cycle (M set) or
 * every 113 2/3 (scanline mode: the prescaler drops by 3 per cycle and adds
 * 341 when it reaches 0 or less). Clocking $FF reloads the latch and asserts
 * the IRQ. Runs in closed form: the bus only calls it on its slow path. */
/* One clock of MMC3's IRQ counter (an A12 rise). RAMBO-1 loads one more
 * after a $C001 reload when the latch is 2 or more (Mesen2's reading, which
 * Hard Drivin' needs). */
static void mmc3_clock(Cart *c, uint64_t dot) {
    if (c->irq_counter == 0 || c->irq_reload) {
        c->irq_counter = (uint8_t)(c->irq_latch + (is_rambo(c) && c->irq_reload && c->irq_latch >= 2));
        c->irq_reload = 0;
    } else {
        c->irq_counter--;
    }
    /* TC0690: about 4 CPU cycles later; the PPU fetches every 2 dots
     * through the rest of the line, so the next one past that asserts */
    if (c->irq_counter == 0 && c->irq_enabled) {
        if (c->mapper == 48) c->irq_due_dot = dot + 12;
        else c->irq_line = 1;
    }
}

/* Counters of CPU cycles: Irem H3001 (65) down to 0, then stops; Sunsoft-3
 * (67) down past 0, then stops; VRC3 (73) up past $FFFF (or the low byte
 * past $FF), reloading; RAMBO-1 (64, 158) in its cycle mode clocks MMC3's
 * counter every 4 cycles. run: n cycles; due: cycles to the IRQ, -1 = none. */
static int is_cycle_counter(const Cart *c) {
    return c->mapper == 65 || c->mapper == 67 || c->mapper == 73 || is_rambo(c);
}

static void cycle_counter_run(Cart *c, int64_t n) {
    switch (c->mapper) {
    case 65:
        if (!c->irq_enabled || !c->irq_count16) return;
        if (n >= c->irq_count16) { c->irq_count16 = 0; c->irq_line = 1; }
        else c->irq_count16 = (uint16_t)(c->irq_count16 - n);
        return;
    case 67:
        if (!c->irq_enabled) return;
        if (n > c->irq_count16) { c->irq_count16 = 0xFFFF; c->irq_enabled = 0; c->irq_line = 1; }
        else c->irq_count16 = (uint16_t)(c->irq_count16 - n);
        return;
    case 73: {
        if (!(c->vrc_irq_ctrl & 2)) return;
        int64_t mask = (c->vrc_irq_ctrl & 4) ? 0xFF : 0xFFFF, cur = c->irq_count16 & mask, latch = c->irq_reload16 & mask;
        int64_t v = cur + n;
        if (v > mask) { c->irq_line = 1; v = latch + (v - mask - 1) % (mask - latch + 1); }
        c->irq_count16 = (uint16_t)((c->irq_count16 & ~mask) | v);
        return;
    }
    default: {
        if (!(c->irq_ctrl & 1)) return;
        int64_t k = (c->irq_prescale + n) / 4;
        c->irq_prescale = (uint8_t)((c->irq_prescale + n) % 4);
        while (k-- > 0) mmc3_clock(c, 0);
    }
    }
}

static int64_t cycle_counter_due(const Cart *c) {
    switch (c->mapper) {
    case 65: return c->irq_enabled && c->irq_count16 ? c->irq_count16 : -1;
    case 67: return c->irq_enabled ? c->irq_count16 + 1 : -1;
    case 73: {
        if (!(c->vrc_irq_ctrl & 2)) return -1;
        int64_t mask = (c->vrc_irq_ctrl & 4) ? 0xFF : 0xFFFF;
        return mask - (c->irq_count16 & mask) + 1;
    }
    default: {
        if (!(c->irq_ctrl & 1) || !c->irq_enabled) return -1;
        uint8_t first = c->irq_reload ? (uint8_t)(c->irq_latch + (c->irq_latch >= 2)) : c->irq_latch;
        int64_t clocks = c->irq_counter && !c->irq_reload ? c->irq_counter : first ? first + 1 : 1;
        return (4 - c->irq_prescale) + 4 * (clocks - 1);
    }
    }
}

void cart_run(Cart *c, uint64_t cycle) {
    if (cycle <= c->irq_cycle) { c->irq_cycle = cycle; return; }
    int64_t n = (int64_t)(cycle - c->irq_cycle);
    c->irq_cycle = cycle;
    if (c->mapper == 20) { fds_run(c, n); return; }
    if (c->mapper == 19) {  /* Namco 163: counts up to $7FFF, then IRQ and stop */
        if (!c->n163_irq_enable || c->n163_counter >= 0x7FFF) return;
        int64_t to = c->n163_counter + n;
        if (to >= 0x7FFF) { to = 0x7FFF; c->irq_line = 1; }
        c->n163_counter = (uint16_t)to;
        return;
    }
    if (is_cycle_counter(c)) { cycle_counter_run(c, n); return; }
    int irq;
    uint16_t mask = count16_mask(c, &irq);
    if (mask) {
        int64_t m = c->irq_count16 & mask;  /* cycles until the wrap, minus one */
        if (n > m && irq) c->irq_line = 1;
        c->irq_count16 = (uint16_t)((c->irq_count16 & ~mask) | ((m - n) & mask));
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
    if (c->mapper == 20) {
        if ((need = fds_cycles_to_irq(c)) < 0) return -1;
    } else if (c->mapper == 19) {
        if (!c->n163_irq_enable || c->n163_counter >= 0x7FFF) return -1;
        need = 0x7FFF - c->n163_counter;
    } else if (is_cycle_counter(c)) {
        need = cycle_counter_due(c);
        if (need < 0) return -1;
    } else if (c->mapper == 18 || c->mapper == 69 || is_bandai(c)) {
        int irq;
        uint16_t mask = count16_mask(c, &irq);
        if (!mask || !irq) return -1;
        need = (c->irq_count16 & mask) + 1;
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
 * $5113 banks the PRG RAM at $6000. PRG RAM write protection ($5102/$5103)
 * is not emulated (sound: runtime/apu.c). */

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
    case 0x5200: c->mmc5_split = v; return 1;
    case 0x5201: c->mmc5_split_scroll = v; return 1;
    case 0x5202: c->mmc5_split_bank = v; return 1;
    default:
        if (a < 0x5120 || a > 0x512B) return 0;  /* RAM protect: ignored (sound: runtime/apu.c) */
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

/* Vertical split ($5200-$5202; timing as Mesen2): screen tile columns 0-32
 * left of $5200 & 31 (bit 6: right of and at it) take ExRAM as their
 * nametable, Y scroll $5201 + the scanline (mod 240) and CHR bank $5202 (4 KB),
 * ignoring the PPU's scroll. Column: dots 321/329 fetch 0 and 1 of the next
 * line, dots 1-256 columns 2-33. The MMC5 sees a new line late: the
 * nametable fetch at dot 1 and the tile at dot 321 still use the line before,
 * and on line 0 the tile at dot 1 is not in frame yet. */
int cart_mmc5_split(const Cart *c, int dot, int phase, uint16_t a, uint8_t *out) {
    if (!(c->mmc5_split & 0x80) || c->mmc5_exram_mode > 1 || !c->mmc5_in_frame) return 0;
    if (c->mmc5_counter == 0 && dot < 9) return 0;
    int col = dot <= 256 ? ((dot - 1) >> 3) + 2 : (dot - 321) >> 3, edge = c->mmc5_split & 31;
    if (col > 32 || ((c->mmc5_split & 0x40) ? col < edge : col >= edge)) return 0;
    int line = c->mmc5_counter + (dot >= 329) - (dot <= 8 && phase == 0);
    unsigned y = (unsigned)(line + c->mmc5_split_scroll) % 240, tile = (y & 0xF8) << 2 | (unsigned)col;
    if (phase == 0) *out = c->exram[tile];
    else if (phase == 2) *out = (c->exram[0x3C0 | (tile & 0x380) >> 4 | (tile & 0x1F) >> 2] >> ((tile >> 4 & 4) | (tile & 2))) & 3;
    else *out = c->chr[((uint32_t)c->mmc5_split_bank * 0x1000 + (((a & ~7u) | (y & 7)) & 0xFFF)) % c->chr_size];
    return 1;
}

/* ---- Namco 163 (19): 1 KB CHR banks at $8000-$B800, nametables at
 * $C000-$D800 ($E0-$FF: nametable RAM page, else a CHR ROM page), 8 KB PRG
 * at $E000/$E800/$F000 with $E000 fixed to the last bank, a 15-bit cycle
 * IRQ at $5000/$5800, 128 bytes of RAM behind $4800 (address at $F800).
 * shortcut: with $E800 bit 6/7 clear, CHR values $E0-$FF select nametable
 * RAM as pattern data; read as CHR ROM here (the 21 games checked either set
 * both bits or never write such values, systems/nes/docs/verification-log.md). The wavetable sound: see
 * cart_n163_clock. */
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
    else if (a < 0xF800) {
        c->n163_prg[(a - 0xE000) >> 11] = v & 0x3F;
        if (a < 0xE800) c->n163_halt = (v >> 6) & 1;
    }
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

/* Namco 163 sound (nesdev "Namco 163 audio"): channel registers at $40-$7F
 * of the 128-byte RAM, channel 8 at $78: frequency (18 bits: $78, $7A, $7C
 * bits 0-1), phase (24 bits: $79, $7B, $7D), length 256 - ($7C & $FC)
 * samples, wave address $7E (in 4-bit samples, low nibble first), volume
 * $7F bits 0-3; $7F bits 4-6 + 1 channels run, 8 downwards. Every 15 CPU
 * cycles the next channel adds its frequency to its phase (modulo length <<
 * 16) and outputs (sample - 8) * volume. The chip outputs one channel at a
 * time; like most emulators this averages them (the wiki's suggestion; with
 * 8 channels the real chip also whines at ~15 kHz). */
int cart_n163_clock(Cart *c) {
    if (++c->n163_div < 15) return 0;
    c->n163_div = 0;
    if (c->n163_halt) return 0;
    int count = ((c->n163_ram[0x7F] >> 4) & 7) + 1;
    if (c->n163_ch >= count) c->n163_ch = 0;
    uint8_t *r = &c->n163_ram[0x78 - 8 * c->n163_ch];
    uint32_t phase = (uint32_t)r[5] << 16 | (uint32_t)r[3] << 8 | r[1];
    uint32_t freq = (uint32_t)(r[4] & 3) << 16 | (uint32_t)r[2] << 8 | r[0];
    uint32_t length = 256u - (r[4] & 0xFC);
    phase = (phase + freq) % (length << 16);
    r[5] = (uint8_t)(phase >> 16), r[3] = (uint8_t)(phase >> 8), r[1] = (uint8_t)phase;
    int x = (int)(((phase >> 16) + r[6]) & 0xFF);
    int s = (c->n163_ram[x >> 1] >> ((x & 1) * 4)) & 0x0F;
    int8_t out = (int8_t)((s - 8) * (r[7] & 0x0F)), old = c->n163_out[c->n163_ch];
    c->n163_out[c->n163_ch] = out;
    c->n163_ch = (uint8_t)((c->n163_ch + 1) % count);
    return out != old;
}

float cart_n163_level(const Cart *c) {
    if (c->n163_halt) return 0;
    int count = ((c->n163_ram[0x7F] >> 4) & 7) + 1, sum = 0;
    for (int i = 0; i < count; i++) sum += c->n163_out[i];
    return (float)sum / (float)count;
}

uint8_t cart_nt_read(Cart *c, const uint8_t *ciram, uint16_t a) {
    int q = (a >> 10) & 3, off = a & 0x3FF;
    if (c->mapper == 118 || c->mapper == 207 || c->mapper == 95 || c->mapper == 158)
        return ciram[c->txsrom_nt[q] * 0x400 + off];
    if (c->mapper == 68) {  /* Sunsoft-4: CIRAM, or CHR ROM pages */
        uint16_t i = cart_nt_index(c, a);
        if (!(c->outer & 0x10)) return ciram[i];
        return c->chr[((0x80u | c->chr_bank[4 + (i >> 10 & 1)]) * 0x400 + (uint32_t)off) % c->chr_rom_size];
    }
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
    if (c->mapper == 118 || c->mapper == 207 || c->mapper == 95 || c->mapper == 158) {
        ciram[c->txsrom_nt[q] * 0x400 + off] = v;
        return;
    }
    if (c->mapper == 68) { if (!(c->outer & 0x10)) ciram[cart_nt_index(c, a)] = v; return; }
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

/* ---- MMC2 (9), MMC4 (10): $A000 PRG (MMC2: 8 KB at $8000, the last three
 * fixed; MMC4: 16 KB, the last fixed), $B000-$E000 4 KB CHR banks for
 * $0000 with latch $FD / $FE, then $1000 with $FD / $FE; $F000 mirroring.
 * Reading tile $FD or $FE's pattern sets the latch of its table (MMC2's left
 * table only on the row-0 high-plane byte, $0FD8/$0FE8). */
static void mmc2_update(Cart *c) {
    uint32_t n8 = c->prg_size / 0x2000;
    if (c->mapper == 9) {
        c->prg_off[0] = (c->prg_bank[0] % n8) * 0x2000;
        for (int w = 1; w < 4; w++) c->prg_off[w] = ((n8 - 4 + (uint32_t)w) % n8) * 0x2000;
    } else {
        set_prg16(c, 0, c->prg_bank[0]);
        set_prg16(c, 1, c->prg_size / 0x4000 - 1);
    }
    set_chr(c, 0, 4, c->chr_bank[c->chr_latch[0]] * 4u);
    set_chr(c, 4, 4, c->chr_bank[2 + c->chr_latch[1]] * 4u);
}

void cart_chr_latch(Cart *c, uint16_t a) {
    uint16_t r = a & 0x0FF8;
    if ((r != 0x0FD8 && r != 0x0FE8) || (c->mapper == 9 && a < 0x1000 && (a & 7))) return;
    uint8_t t = (a >> 12) & 1, l = r == 0x0FE8;
    if (c->chr_latch[t] != l) { c->chr_latch[t] = l; mmc2_update(c); }
}

static void mmc2_write(Cart *c, uint16_t a, uint8_t v) {
    if (a < 0xA000) return;
    if (a < 0xB000) c->prg_bank[0] = v & 0x0F;
    else if (a < 0xF000) c->chr_bank[(a - 0xB000) >> 12] = v & 0x1F;
    else if (!c->four_screen) c->mirroring = (v & 1) ? MIRROR_HORIZONTAL : MIRROR_VERTICAL;
    mmc2_update(c);
}

/* 8 KB PRG banks at $8000-$DFFF, the last fixed at $E000, eight 1 KB CHR
 * banks (SS88006, FME-7). */
static void banks8_update(Cart *c) {
    uint32_t n8 = c->prg_size / 0x2000;
    for (int w = 0; w < 3; w++) c->prg_off[w] = (c->prg_bank[w] % n8) * 0x2000;
    c->prg_off[3] = (n8 - 1) * 0x2000;
    for (int i = 0; i < 8; i++) set_chr(c, i, 1, c->chr_bank[i]);
    if (c->mapper == 69) {  /* command 8: bit 6 RAM (else PRG ROM bank bits 0-5) at $6000 */
        c->rom_at_6000 = !(c->fme7_6000 & 0x40);
        c->ram_off = c->rom_at_6000 ? ((c->fme7_6000 & 0x3Fu) % n8) * 0x2000 : 0;
    }
}

/* ---- Konami VRC1 (75): $8000/$A000/$C000 8 KB PRG (the last bank fixed at
 * $E000), $E000/$F000 the low 4 bits of the 4 KB CHR banks at $0000/$1000,
 * $9000 mirroring (bit 0: 0 vertical, 1 horizontal; ignored on four-screen
 * boards) and the CHR banks' bit 4 (bit 1: $0000, bit 2: $1000). No RAM or
 * IRQ. The chip has 4 PRG bank bits (128 KB); a fifth is kept for the
 * 256 KB re-release of Ninja JaJaMaru (Switch collection, NES 2.0 header),
 * which boots only with it; on 128 KB it vanishes in the bank modulo. */
static void vrc1_update(Cart *c) {
    uint32_t n8 = c->prg_size / 0x2000;
    for (int w = 0; w < 3; w++) c->prg_off[w] = (c->prg_bank[w] % n8) * 0x2000;
    c->prg_off[3] = (n8 - 1) * 0x2000;
    set_chr(c, 0, 4, c->chr_bank[0] * 4u);
    set_chr(c, 4, 4, c->chr_bank[1] * 4u);
}

static void vrc1_write(Cart *c, uint16_t a, uint8_t v) {
    switch (a >> 12) {
    case 0x8: case 0xA: case 0xC: c->prg_bank[((a >> 12) - 8) >> 1] = v & 0x1F; break;
    case 0x9:
        if (!c->four_screen) c->mirroring = (v & 1) ? MIRROR_HORIZONTAL : MIRROR_VERTICAL;
        c->chr_bank[0] = (uint8_t)((c->chr_bank[0] & 0x0F) | (v & 2) << 3);
        c->chr_bank[1] = (uint8_t)((c->chr_bank[1] & 0x0F) | (v & 4) << 2);
        break;
    case 0xE: case 0xF: {
        uint8_t *b = &c->chr_bank[(a >> 12) - 0xE];
        *b = (uint8_t)((*b & 0x10) | (v & 0x0F));
        break;
    }
    }
    vrc1_update(c);
}

/* ---- Irem G-101 (32): $8000 and $A000 8 KB PRG; $9000 bit 1 swaps the
 * $8000 bank with $C000 (fixed to the second-to-last bank), bit 0 mirroring
 * (0 vertical, 1 horizontal); $B000-$B007 1 KB CHR banks; $E000 fixed to the
 * last bank. Submapper 1 (Major League): $9000 is not connected and CIRAM
 * A10 is tied high (one screen, page 1). */
static void g101_update(Cart *c) {
    uint32_t n8 = c->prg_size / 0x2000;
    int swap = (c->latch & 2) != 0;  /* $9000: always 0 on submapper 1 */
    c->prg_off[swap ? 2 : 0] = (c->prg_bank[0] % n8) * 0x2000;
    c->prg_off[swap ? 0 : 2] = (n8 - 2) * 0x2000;
    c->prg_off[1] = (c->prg_bank[1] % n8) * 0x2000;
    c->prg_off[3] = (n8 - 1) * 0x2000;
    for (int i = 0; i < 8; i++) set_chr(c, i, 1, c->chr_bank[i]);
}

static void g101_write(Cart *c, uint16_t a, uint8_t v) {
    switch (a & 0xF000) {
    case 0x8000: c->prg_bank[0] = v & 0x1F; break;
    case 0xA000: c->prg_bank[1] = v & 0x1F; break;
    case 0xB000: c->chr_bank[a & 7] = v; break;
    case 0x9000:
        if (c->submapper == 1) break;
        c->latch = v;
        if (!c->four_screen) c->mirroring = (v & 1) ? MIRROR_HORIZONTAL : MIRROR_VERTICAL;
        break;
    }
    g101_update(c);
}

/* ---- Taito: TC0190 (33) and TC0690 (48) write $8000/$8001 8 KB PRG at
 * $8000/$A000 (6 bits), $8002/$8003 2 KB CHR (numbered in 2 KB units),
 * $A000-$A003 1 KB CHR; $C000 and $E000 are fixed to the last two banks.
 * 33: $8000 bit 6 mirroring (0 vertical, 1 horizontal). 48: $E000 bit 6
 * mirroring, and $C000-$C003 drive MMC3's IRQ ($C000 latch inverted, $C001
 * reload, $C002 enable, $C003 disable and acknowledge), which fires about 4
 * CPU cycles after MMC3's would (games shake without the delay).
 * X1-005 (80, 207): registers at $7EF0-$7EFF ($7E70 mirrors): $7EF0/$7EF1 2
 * KB CHR (1 KB numbers, bit 0 ignored), $7EF2-$7EF5 1 KB CHR, $7EF6 bit 0
 * mirroring (0 horizontal, 1 vertical; 80 only), $7EF8/$7EF9 $A3 enables the
 * 128 bytes of RAM at $7F00 (mirrored at $7F80), $7EFA/$7EFC/$7EFE (and the
 * odd mirrors) 8 KB PRG at $8000/$A000/$C000; $E000 is fixed to the last
 * bank. 207 wires $7EF0/$7EF1 bit 7 to the nametables instead: $2000/$2400
 * and $2800/$2C00, like TxSROM. */
static int is_x1005(const Cart *c) { return c->mapper == 80 || c->mapper == 207; }

static void taito_update(Cart *c) {
    uint32_t n8 = c->prg_size / 0x2000;
    int x1005 = is_x1005(c);
    for (int w = 0; w < 3; w++) c->prg_off[w] = ((w < 2 || x1005 ? c->prg_bank[w] : n8 - 2) % n8) * 0x2000;
    c->prg_off[3] = (n8 - 1) * 0x2000;
    for (int i = 0; i < 2; i++)
        set_chr(c, i * 2, 2, x1005 ? c->chr_bank[i] & (c->mapper == 207 ? 0x7E : 0xFE) : c->chr_bank[i] * 2u);
    for (int i = 2; i < 6; i++) set_chr(c, i + 2, 1, c->chr_bank[i]);
}

static void mmc3_write(Cart *c, uint16_t a, uint8_t v);

static void taito_write(Cart *c, uint16_t a, uint8_t v) {
    int r = a & 3, mirror = -1;
    switch (a & 0xE000) {
    case 0x8000:
        if (r < 2) c->prg_bank[r] = v & 0x3F;
        else c->chr_bank[r - 2] = v;
        if (r == 0 && c->mapper == 33) mirror = v & 0x40;
        break;
    case 0xA000: c->chr_bank[2 + r] = v; break;
    case 0xC000:  /* as MMC3's $C000, $C001, $E001, $E000 */
        if (c->mapper == 48) mmc3_write(c, (uint16_t)(r < 2 ? 0xC000 | r : 0xE000 | (r == 2)), r == 0 ? v ^ 0xFF : v);
        break;
    case 0xE000: if (c->mapper == 48 && r == 0) mirror = v & 0x40; break;
    }
    if (mirror >= 0 && !c->four_screen) c->mirroring = mirror ? MIRROR_HORIZONTAL : MIRROR_VERTICAL;
    taito_update(c);
}

static void x1005_write(Cart *c, uint16_t a, uint8_t v) {
    int r = a & 0x0F;
    if (r < 6) c->chr_bank[r] = v;
    else if (r == 6 && c->mapper == 80 && !c->four_screen) c->mirroring = (v & 1) ? MIRROR_VERTICAL : MIRROR_HORIZONTAL;
    else if (r == 8 || r == 9) c->latch = v;  /* RAM enable */
    else if (r >= 10) c->prg_bank[(r - 10) >> 1] = v & 0x3F;
    if (c->mapper == 207 && r < 2) c->txsrom_nt[r * 2] = c->txsrom_nt[r * 2 + 1] = v >> 7;
    taito_update(c);
}

/* ---- Jaleco SS88006 (18): decodes A12-A14 and A0-A1, data D0-D3, so each
 * bank number is written as two nibbles (low first). $8000-$9001 PRG,
 * $A000-$D003 CHR, $E000-$E003 IRQ reload (low nibble first), $F000 reload
 * and acknowledge, $F001 IRQ control (acknowledges), $F002 mirroring. The
 * PRG RAM protect ($9002) and the speech chip ($F003) are not emulated. */
static void ss88006_write(Cart *c, uint16_t a, uint8_t v) {
    int k = ((a >> 12) & 7) * 4 + (a & 3), sh = (k & 1) * 4;
    v &= 0x0F;
    if (k < 6) c->prg_bank[k >> 1] = (uint8_t)((c->prg_bank[k >> 1] & ~(0xF << sh)) | v << sh);
    else if (k >= 8 && k < 24) c->chr_bank[(k - 8) >> 1] = (uint8_t)((c->chr_bank[(k - 8) >> 1] & ~(0xF << sh)) | v << sh);
    else if (k >= 24 && k < 28) {
        sh = (k - 24) * 4;
        c->irq_reload16 = (uint16_t)((c->irq_reload16 & ~(0xF << sh)) | v << sh);
    } else if (k == 28) { c->irq_count16 = c->irq_reload16; c->irq_line = 0; }
    else if (k == 29) { c->irq_ctrl = v; c->irq_line = 0; }
    else if (k == 30 && !c->four_screen) {
        static const int MIRROR[4] = {MIRROR_HORIZONTAL, MIRROR_VERTICAL, MIRROR_SINGLE_LOW, MIRROR_SINGLE_HIGH};
        c->mirroring = MIRROR[v & 3];
    }
    banks8_update(c);
}

/* ---- Sunsoft FME-7 (69): command ($8000) then parameter ($A000).
 * 0-7 CHR, 8 $6000 (ROM bank or RAM), 9-B PRG, C mirroring, D IRQ control
 * (bit 0 IRQ, bit 7 counting; acknowledges), E/F counter low/high (no
 * reload latch). shortcut: RAM selected but disabled (command 8 = $40-$7F)
 * reads as RAM, not open bus; no game is known to rely on it. The 5B sound
 * ($C000/$E000, Gimmick!) is not emulated. */
static void fme7_write(Cart *c, uint16_t a, uint8_t v) {
    if (a < 0xA000) { c->fme7_cmd = v & 0x0F; return; }
    if (a >= 0xC000) return;  /* 5B sound: runtime/apu.c */
    uint8_t cmd = c->fme7_cmd;
    if (cmd < 8) c->chr_bank[cmd] = v;
    else if (cmd == 8) c->fme7_6000 = v;
    else if (cmd < 0xC) c->prg_bank[cmd - 9] = v & 0x3F;
    else if (cmd == 0xC && !c->four_screen) {
        static const int MIRROR[4] = {MIRROR_VERTICAL, MIRROR_HORIZONTAL, MIRROR_SINGLE_LOW, MIRROR_SINGLE_HIGH};
        c->mirroring = MIRROR[v & 3];
    }
    else if (cmd == 0xD) { c->irq_ctrl = v; c->irq_line = 0; }
    else if (cmd == 0xE) c->irq_count16 = (uint16_t)((c->irq_count16 & 0xFF00) | v);
    else if (cmd == 0xF) c->irq_count16 = (uint16_t)((c->irq_count16 & 0x00FF) | v << 8);
    banks8_update(c);
}

/* ---- Namco 175/340 (210): the Namco 163's CHR and PRG registers, without
 * its nametables, IRQ and sound. The 340 sets mirroring with $E000 bits 6-7;
 * the 175's is wired (its $C000 RAM enable is ignored). Without a submapper:
 * a battery means 175 (true for every commercial game, nesdev). */
static void n175_write(Cart *c, uint16_t a, uint8_t v) {
    if ((a >= 0xC000 && a < 0xE000) || a >= 0xF800) return;
    int is340 = c->submapper == 2 || (c->submapper != 1 && !c->battery);
    if (a >= 0xE000 && a < 0xE800 && is340 && !c->four_screen) {
        static const int MIRROR[4] = {MIRROR_SINGLE_LOW, MIRROR_VERTICAL, MIRROR_SINGLE_HIGH, MIRROR_HORIZONTAL};
        c->mirroring = MIRROR[v >> 6];
    }
    n163_write(c, a, v);
}

/* ---- I2C serial EEPROMs (Bandai 16, 157, 159): START = SDA falls while SCL
 * is high, STOP = SDA rises; a bit per SCL rise, MSB first; after 8 bits an
 * acknowledge clock (the receiver pulls SDA low). 24C02: device byte $A0
 * (write) / $A1 (read), then for writes a word address and data (8-byte
 * pages. Reads go on while the CPU acknowledges each byte. The chip changes
 * its SDA while SCL is low.
 * X24C01 bit order, on purpose: the chip takes address and data MSB first,
 * but Bandai's code sends them LSB first (nesdev "INES Mapper 159": the
 * X24C01 "is actually still a big-endian device, and multi-byte reads and
 * writes increment the address as MSB-first"). So the game's address A is
 * the chip's bit-reversed A, and a sequential write from A goes on at
 * reversed(A) + 1, not at the game's A + 1. That is what the games ran on;
 * decoding LSB first (as some emulators do) would differ from it whenever
 * a sequential access is mixed with a random one. Saves (.sav) hold the
 * bytes in the chip's order. */
enum { EE_IDLE, EE_DEVICE, EE_ADDR, EE_WRITE, EE_READ };

static void ee_rise(Eeprom *e, int sda) {
    if (e->state == EE_IDLE) return;
    if (e->bit < 8) {
        if (e->state != EE_READ) e->shift = (uint8_t)(e->shift << 1 | sda);
        e->bit++;
        return;
    }
    if (e->state == EE_READ && sda) e->next = EE_IDLE;  /* no acknowledge: the CPU is done */
    e->state = e->next;
    e->bit = 0;
}

static void ee_fall(Cart *c, Eeprom *e) {
    uint8_t *m = c->prg_ram + e->base;
    e->out = 1;
    if (e->state == EE_READ) {
        if (e->bit < 8) e->out = (m[e->addr] >> (7 - e->bit)) & 1;
        else { e->next = EE_READ; e->addr = (uint8_t)((e->addr + 1) % e->size); }
        return;
    }
    if (e->state == EE_IDLE || e->bit < 8) return;
    uint8_t b = e->shift, page = e->c01 ? 3 : 7;
    e->out = 0;  /* acknowledge the byte */
    if (e->state == EE_DEVICE) {
        if ((b & 0xFE) == 0xA0) e->next = (b & 1) ? EE_READ : EE_ADDR;
        else { e->out = 1; e->next = EE_IDLE; }  /* another device */
    } else if (e->state == EE_ADDR && e->c01) {
        e->addr = (uint8_t)((b >> 1) % e->size);
        e->next = (b & 1) ? EE_READ : EE_WRITE;
    } else if (e->state == EE_ADDR) {
        e->addr = (uint8_t)(b % e->size);
        e->next = EE_WRITE;
    } else {
        m[e->addr] = b;
        e->addr = (uint8_t)((e->addr & ~page) | ((e->addr + 1) & page));
        e->next = EE_WRITE;
    }
}

static void ee_lines(Cart *c, Eeprom *e, int scl, int sda) {
    if (e->scl && scl && e->sda != sda) {  /* SDA moved while SCL is high */
        e->state = sda ? EE_IDLE : (e->c01 ? EE_ADDR : EE_DEVICE);
        e->bit = 0;
        e->out = 1;
    } else if (!e->scl && scl) {
        ee_rise(e, sda);
    } else if (e->scl && !scl) {
        ee_fall(c, e);
    }
    e->scl = (uint8_t)scl;
    e->sda = (uint8_t)sda;
}

/* ---- Bandai FCG (16, 153, 157, 159). Registers by the low 4 address bits:
 * 0-7 1 KB CHR banks, 8 16 KB PRG at $8000 (the last bank fixed at $C000),
 * 9 mirroring, A IRQ control (bit 0 counting; acknowledges), B/C IRQ counter
 * low/high, D EEPROM lines (bit 5 SCL, 6 SDA). The FCG-1/2 decodes them at
 * $6000-$7FFF and B/C load the counter; the LZ93D50 at $8000-$FFFF, B/C
 * load a latch that A copies into the counter. Mapper 16 with submapper 4
 * is an FCG, 5 an LZ93D50; without a submapper both ranges work, each the
 * way its chip does. 153: CHR RAM, $8000-$8003 bit 0 the 256 KB half of
 * PRG, 8 KB battery RAM ($800D bit 5 RAM enable ignored). 157 (Datach):
 * CHR RAM, $8000-$8003 bit 3 clocks the cartridge's X24C01, $6000 bit 3
 * the barcode reader (0 here: no barcode is ever scanned). */
static void fcg_update(Cart *c) {
    uint32_t outer = 0;
    if (c->mapper == 153) outer = (uint32_t)((c->chr_bank[0] | c->chr_bank[1] | c->chr_bank[2] | c->chr_bank[3]) & 1) << 4;
    set_prg16(c, 0, outer | (c->prg_bank[0] & 0x0Fu));
    set_prg16(c, 1, outer | 0x0Fu);
    for (int i = 0; i < 8; i++) set_chr(c, i, 1, c->chr_ram ? (uint32_t)i : c->chr_bank[i]);
}

static void fcg_write(Cart *c, uint16_t a, uint8_t v) {
    int lz = a >= 0x8000, r = a & 0x0F;
    if (r < 8) {
        c->chr_bank[r] = v;
        if (c->mapper == 157 && r < 4 && c->eeprom_count > 1) ee_lines(c, &c->eeprom[1], (v >> 3) & 1, c->eeprom[1].sda);
    } else if (r == 8) {
        c->prg_bank[0] = v;
    } else if (r == 9 && !c->four_screen) {
        static const int MIRROR[4] = {MIRROR_VERTICAL, MIRROR_HORIZONTAL, MIRROR_SINGLE_LOW, MIRROR_SINGLE_HIGH};
        c->mirroring = MIRROR[v & 3];
    } else if (r == 0xA) {
        c->irq_ctrl = v & 1;
        c->irq_line = 0;
        if (lz) c->irq_count16 = c->irq_reload16;
    } else if (r == 0xB || r == 0xC) {
        uint16_t *t = lz ? &c->irq_reload16 : &c->irq_count16;
        *t = r == 0xB ? (uint16_t)((*t & 0xFF00) | v) : (uint16_t)((*t & 0x00FF) | v << 8);
    } else if (r == 0xD && lz && c->eeprom_count) {
        int sda = (v >> 6) & 1;
        ee_lines(c, &c->eeprom[0], (v >> 5) & 1, sda);
        if (c->eeprom_count > 1) ee_lines(c, &c->eeprom[1], c->eeprom[1].scl, sda);  /* SDA is shared */
    }
    fcg_update(c);
}

/* ---- More boards (nesdev wiki, each "INES Mapper NNN" page and the chip
 * pages named here). Discrete latch boards: one register, written at
 * $8000-$FFFF unless noted (bus conflicts ignored), banks in update_banks:
 *  13 CPROM: 16 KB CHR RAM, bits 0-1 the 4 KB at $1000 ($0000 fixed).
 *  34 BNROM (submapper 2): 32 KB PRG. NINA-001 (1; without a submapper:
 *     CHR ROM over 8 KB): $7FFD 32 KB PRG, $7FFE/$7FFF 4 KB CHR, RAM at $6000.
 *  41 Caltron 6-in-1: $6000-$67FF address bits 0-2 32 KB PRG, 3-4 CHR bits
 *     2-3, 5 mirroring; $8000 bits 0-1 CHR, only while address bit 2 is set.
 *  70 Bandai: bits 4-7 16 KB PRG at $8000, 0-3 8 KB CHR. 152: PRG bits 4-6,
 *     bit 7 one-screen page.
 *  72/92 Jaleco JF-17/JF-19: bit 7 loads bits 0-3 as the 16 KB PRG bank
 *     ($8000; 92: $C000, the first bank at $8000), bit 6 as the 8 KB CHR bank.
 *  77 Irem: bits 0-3 32 KB PRG, 4-7 2 KB CHR ROM at $0000; $0800-$1FFF CHR
 *     RAM, four nametables.
 *  78 Irem/Jaleco: bits 0-2 16 KB PRG, bit 3 mirroring (submapper 3 Holy
 *     Diver: horizontal/vertical; else one-screen), bits 4-7 8 KB CHR.
 *  86 Jaleco JF-13: $6000-$6FFF bits 4-5 32 KB PRG, bits 0-1 and 6 CHR.
 *  89 Sunsoft-2: bits 4-6 16 KB PRG, 0-2 and 7 CHR, 3 one-screen page.
 *  93 Sunsoft-2: bits 4-6 16 KB PRG (CHR RAM). 94 UN1ROM: bits 2-4.
 *  97 Irem TAM-S1: $8000-$BFFF bits 0-4 16 KB at $C000 (the last at
 *     $8000), bits 6-7 mirroring.
 * 113 HES: $4100 (A8 set) bits 3-5 32 KB PRG, 0-2 and 6 CHR, 7 mirroring.
 * 140 Jaleco JF-11/14: $6000-$7FFF bits 4-5 32 KB PRG, 0-3 8 KB CHR.
 * 180 Nichibutsu: bits 0-2 16 KB at $C000, the first bank at $8000.
 * 184 Sunsoft-1: $6000-$7FFF bits 0-2 4 KB CHR at $0000, 4-6 at $1000.
 * 188 Bandai Karaoke Studio: bit 4 set: bits 0-2 16 KB PRG; clear: the
 *     expansion cartridge's bank 8 + bits 0-2; $C000 bank 7; bit 5
 *     mirroring. $6000 reads the microphone buttons (never pressed here).
 * 228 Active Enterprises (Action 52): the address latches: A6-A10 16 KB
 *     PRG, A11-A12 the 512 KB chip (3 = 2), A5 16 KB mode, A13 mirroring,
 *     A0-A3 and data bits 0-1 CHR.
 * 232 Camerica Quattro: $8000-$BFFF bits 3-4 the 64 KB block (submapper 1:
 *     swapped), $C000 bits 0-1 the 16 KB in it; $C000 maps the block's last. */
static void latch_mirroring(Cart *c, uint8_t v) {
    static const int ONE[2] = {MIRROR_SINGLE_LOW, MIRROR_SINGLE_HIGH}, HV[2] = {MIRROR_HORIZONTAL, MIRROR_VERTICAL};
    if (c->four_screen) return;
    switch (c->mapper) {
    case 78: c->mirroring = c->submapper == 3 ? HV[(v >> 3) & 1] : ONE[(v >> 3) & 1]; break;
    case 89: c->mirroring = ONE[(v >> 3) & 1]; break;
    case 152: c->mirroring = ONE[v >> 7]; break;
    case 154: c->mirroring = ONE[(v >> 6) & 1]; break;
    case 113: c->mirroring = HV[v >> 7]; break;
    case 188: c->mirroring = HV[!(v & 0x20)]; break;
    case 97: {
        static const int M[4] = {MIRROR_SINGLE_LOW, MIRROR_HORIZONTAL, MIRROR_VERTICAL, MIRROR_SINGLE_HIGH};
        c->mirroring = M[v >> 6];
        break;
    }
    }
}

/* ---- Irem H3001 (65): $8000/$A000/$C000 8 KB PRG ($C000 starts at the
 * second-to-last bank), $E000 the last; $9001 bit 7 mirroring (1
 * horizontal); $B000-$B007 1 KB CHR; IRQ: $9005/$9006 reload high/low,
 * $9004 reloads, $9003 bit 7 enables (both acknowledge). The counter counts
 * CPU cycles down to 0, asserts the IRQ there and stops. */
static void h3001_write(Cart *c, uint16_t a, uint8_t v) {
    switch (a & 0xF007) {
    case 0x8000: c->prg_bank[0] = v; break;
    case 0xA000: c->prg_bank[1] = v; break;
    case 0xC000: c->prg_bank[2] = v; break;
    case 0x9001: if (!c->four_screen) c->mirroring = (v & 0x80) ? MIRROR_HORIZONTAL : MIRROR_VERTICAL; break;
    case 0x9003: c->irq_enabled = v >> 7; c->irq_line = 0; break;
    case 0x9004: c->irq_count16 = c->irq_reload16; c->irq_line = 0; break;
    case 0x9005: c->irq_reload16 = (uint16_t)((c->irq_reload16 & 0x00FF) | v << 8); break;
    case 0x9006: c->irq_reload16 = (uint16_t)((c->irq_reload16 & 0xFF00) | v); break;
    default: if ((a & 0xF000) == 0xB000) c->chr_bank[a & 7] = v; break;
    }
    banks8_update(c);
}

/* ---- Sunsoft-3 (67): $8800/$9800/$A800/$B800 2 KB CHR; $C800 the IRQ
 * counter, high byte then low; $D800 bit 4 counting (resets the byte order);
 * $E800 mirroring (vertical, horizontal, one screen A, B); $F800 16 KB PRG at
 * $8000, the last fixed; $8000 acknowledges. The counter counts CPU cycles
 * down; wrapping past 0 asserts the IRQ and stops it. */
static void sunsoft3_write(Cart *c, uint16_t a, uint8_t v) {
    static const int MIRROR[4] = {MIRROR_VERTICAL, MIRROR_HORIZONTAL, MIRROR_SINGLE_LOW, MIRROR_SINGLE_HIGH};
    switch (a & 0xF800) {
    case 0x8000: c->irq_line = 0; break;
    case 0x8800: case 0x9800: case 0xA800: case 0xB800: c->chr_bank[(a >> 12) - 8] = v; break;
    case 0xC800:
        c->irq_count16 = c->irq_toggle ? (uint16_t)((c->irq_count16 & 0xFF00) | v)
                                       : (uint16_t)((c->irq_count16 & 0x00FF) | v << 8);
        c->irq_toggle ^= 1;
        break;
    case 0xD800: c->irq_enabled = (v >> 4) & 1; c->irq_toggle = 0; break;
    case 0xE800: if (!c->four_screen) c->mirroring = MIRROR[v & 3]; break;
    case 0xF800: c->prg_bank[0] = v; break;
    }
}

/* ---- Sunsoft-4 (68): $8000-$B000 2 KB CHR; $C000/$D000 the CHR ROM banks
 * (1 KB, bit 7 forced) shown as nametables A/B when $E000 bit 4 is set;
 * $E000 bits 0-1 mirroring (vertical, horizontal, one screen A, B); $F000
 * 16 KB PRG at $8000, the last fixed. Submapper 1 (Nantettatte!! Baseball)
 * with its 16 KB sub-cartridge after the 128 KB: $F000 bit 3 clear maps it.
 * shortcut: the sub-cartridge's licence timer (its ROM reads only for a
 * while after a $6000-$7FFF write) and $F000 bit 4 (PRG RAM enable) are not
 * emulated: the two sub-cartridge dumps report a data error at the title
 * (the game alone runs). Upgrade by adding the timer and open bus. */
static void sunsoft4_write(Cart *c, uint16_t a, uint8_t v) {
    static const int MIRROR[4] = {MIRROR_VERTICAL, MIRROR_HORIZONTAL, MIRROR_SINGLE_LOW, MIRROR_SINGLE_HIGH};
    int r = (a >> 12) - 8;
    if (r < 6) c->chr_bank[r] = v;
    else if (r == 6) { c->outer = v; if (!c->four_screen) c->mirroring = MIRROR[v & 3]; }
    else c->prg_bank[0] = v;
}

/* ---- Konami VRC3 (73): $8000-$B000 the 16-bit IRQ latch, a nibble each
 * (low first); $C000 IRQ control (bit 0 A, 1 E: counting, reloads, 2 M: 8-bit
 * mode; acknowledges); $D000 acknowledge (E = A); $F000 bits 0-2 16 KB PRG at
 * $8000, the last fixed. The counter counts CPU cycles up; overflowing (the
 * low byte in 8-bit mode) reloads the latch and asserts the IRQ. */
static void vrc3_write(Cart *c, uint16_t a, uint8_t v) {
    int r = (a >> 12) - 8, sh = r * 4;
    if (r < 4) c->irq_reload16 = (uint16_t)((c->irq_reload16 & ~(0xF << sh)) | (v & 0xF) << sh);
    else if (r == 4) {
        c->vrc_irq_ctrl = v & 7;
        c->irq_line = 0;
        if (v & 2) c->irq_count16 = c->irq_reload16;
    } else if (r == 5) vrc_irq_ack(c);
    else if (r == 7) c->prg_bank[0] = v & 7;
}

/* ---- Taito X1-017 (82): registers at $7EF0-$7EFF: $7EF0/$7EF1 2 KB CHR
 * (1 KB numbers, bit 0 ignored), $7EF2-$7EF5 1 KB CHR, $7EF6 bit 0
 * mirroring (1 vertical), bit 1 swaps the CHR halves; $7EF7-$7EF9 = $CA, $69,
 * $84 enable the 5 KB of RAM at $6000-$67FF, $6800-$6FFF, $7000-$73FF;
 * $7EFA-$7EFC bits 2-7 8 KB PRG at $8000/$A000/$C000, the last fixed.
 * shortcut: its IRQ ($7EFD-$7EFF) is not emulated; no game is known to use it. */
static int x1017_ram_on(const Cart *c, uint16_t a) {
    return a < 0x6800 ? c->reg[0] == 0xCA : a < 0x7000 ? c->reg[1] == 0x69 : a < 0x7400 && c->reg[2] == 0x84;
}

static void x1017_write(Cart *c, uint16_t a, uint8_t v) {
    int r = a & 0x0F;
    if (r < 6) c->chr_bank[r] = v;
    else if (r == 6) { c->latch = v; if (!c->four_screen) c->mirroring = (v & 1) ? MIRROR_VERTICAL : MIRROR_HORIZONTAL; }
    else if (r < 10) c->reg[r - 7] = v;
    else if (r < 13) c->prg_bank[r - 10] = v >> 2;
}

static void x1017_update(Cart *c) {
    uint32_t n8 = c->prg_size / 0x2000;
    for (int w = 0; w < 3; w++) c->prg_off[w] = (c->prg_bank[w] % n8) * 0x2000;
    c->prg_off[3] = (n8 - 1) * 0x2000;
    int inv = (c->latch & 2) ? 4 : 0;
    set_chr(c, 0 ^ inv, 2, c->chr_bank[0] & 0xFEu);
    set_chr(c, 2 ^ inv, 2, c->chr_bank[1] & 0xFEu);
    for (int i = 0; i < 4; i++) set_chr(c, (4 + i) ^ inv, 1, c->chr_bank[2 + i]);
}

static void update_banks(Cart *c) {
    uint32_t n16 = c->prg_size / 0x4000;
    switch (c->mapper) {
    case 0: set_prg16(c, 0, 0); set_prg16(c, 1, n16 - 1); set_chr(c, 0, 8, 0); break;
    case 1: mmc1_update(c); break;
    case 2: set_prg16(c, 0, c->latch); set_prg16(c, 1, n16 - 1); set_chr(c, 0, 8, 0); break;
    case 3: set_prg16(c, 0, 0); set_prg16(c, 1, n16 - 1); set_chr(c, 0, 8, (uint32_t)c->latch * 8); break;
    case 4: case 118: case 37: case 47: case 64: case 158: case 119:
    case 76: case 88: case 95: case 154: mmc3_update(c); break;
    case 155: mmc1_update(c); break;  /* MMC1A */
    case 65: banks8_update(c); break;
    case 82: x1017_update(c); break;
    case 67: case 68: {
        int sub = c->mapper == 68 && n16 == 9;  /* 68 + its 16 KB sub-cartridge after the 128 KB */
        set_prg16(c, 0, sub ? ((c->prg_bank[0] & 8) ? c->prg_bank[0] & 7u : 8) : c->prg_bank[0] & 0x0Fu);
        set_prg16(c, 1, sub ? 7 : n16 - 1);
        for (int i = 0; i < 4; i++) set_chr(c, i * 2, 2, c->chr_bank[i] * 2u);
        break;
    }
    case 73: set_prg16(c, 0, c->prg_bank[0]); set_prg16(c, 1, n16 - 1); set_chr(c, 0, 8, 0); break;
    case 13: set_prg32(c, 0); set_chr(c, 0, 4, 0); set_chr(c, 4, 4, (c->latch & 3u) * 4); break;
    case 34:
        if (c->submapper == 1) {
            set_prg32(c, c->reg[0]);
            set_chr(c, 0, 4, c->reg[1] * 4u);
            set_chr(c, 4, 4, c->reg[2] * 4u);
        } else {
            set_prg32(c, c->latch);
            set_chr(c, 0, 8, 0);
        }
        break;
    case 41: {
        uint32_t chr = (c->outer >> 1 & 0x0Cu) | (c->latch & 3u);
        set_prg32(c, c->outer & 7);
        set_chr(c, 0, 8, chr * 8);
        break;
    }
    case 70: case 152:
        set_prg16(c, 0, (c->latch >> 4) & (c->mapper == 70 ? 0x0F : 7));
        set_prg16(c, 1, n16 - 1);
        set_chr(c, 0, 8, (c->latch & 0x0Fu) * 8);
        break;
    case 72: case 92:
        set_prg16(c, 0, c->mapper == 72 ? c->prg_bank[0] : 0);
        set_prg16(c, 1, c->mapper == 72 ? n16 - 1 : c->prg_bank[0]);
        set_chr(c, 0, 8, c->chr_bank[0] * 8u);
        break;
    case 77:
        set_prg32(c, c->latch & 0x0F);
        set_chr(c, 0, 2, (c->latch >> 4) * 2u);
        for (int i = 2; i < 8; i++) c->chr_off[i] = c->chr_rom_size + (uint32_t)i * 0x400;  /* CHR RAM */
        break;
    case 78: set_prg16(c, 0, c->latch & 7); set_prg16(c, 1, n16 - 1); set_chr(c, 0, 8, (c->latch >> 4) * 8u); break;
    case 86: set_prg32(c, (c->latch >> 4) & 3); set_chr(c, 0, 8, ((c->latch & 3u) | (c->latch >> 4 & 4u)) * 8); break;
    case 89:
        set_prg16(c, 0, (c->latch >> 4) & 7); set_prg16(c, 1, n16 - 1);
        set_chr(c, 0, 8, ((c->latch & 7u) | (c->latch >> 4 & 8u)) * 8);
        break;
    case 93: set_prg16(c, 0, (c->latch >> 4) & 7); set_prg16(c, 1, n16 - 1); set_chr(c, 0, 8, 0); break;
    case 94: set_prg16(c, 0, (c->latch >> 2) & 7); set_prg16(c, 1, n16 - 1); set_chr(c, 0, 8, 0); break;
    case 97: set_prg16(c, 0, n16 - 1); set_prg16(c, 1, c->latch & 0x1F); set_chr(c, 0, 8, 0); break;
    case 113: set_prg32(c, (c->latch >> 3) & 7); set_chr(c, 0, 8, ((c->latch & 7u) | (c->latch >> 3 & 8u)) * 8); break;
    case 140: set_prg32(c, (c->latch >> 4) & 3); set_chr(c, 0, 8, (c->latch & 0x0Fu) * 8); break;
    case 180: set_prg16(c, 0, 0); set_prg16(c, 1, c->latch & 7); set_chr(c, 0, 8, 0); break;
    case 184:
        set_prg16(c, 0, 0); set_prg16(c, 1, n16 - 1);
        set_chr(c, 0, 4, (c->latch & 7u) * 4);
        set_chr(c, 4, 4, (c->latch >> 4 & 7u) * 4);
        break;
    case 188:
        set_prg16(c, 0, (c->latch & 0x10) ? c->latch & 7u : (c->latch & 7u) | 8);
        set_prg16(c, 1, 7);
        set_chr(c, 0, 8, 0);
        break;
    case 228: {
        uint32_t a = c->addr_latch, chip = (a >> 11) & 3, page = ((a >> 6) & 0x1F) | (chip == 3 ? 2 : chip) << 5;
        set_prg16(c, 0, (a & 0x20) ? page : page & ~1u);
        set_prg16(c, 1, (a & 0x20) ? page : page | 1u);
        set_chr(c, 0, 8, ((a & 0x0F) << 2 | (c->latch & 3u)) * 8);
        break;
    }
    case 232:
        set_prg16(c, 0, c->outer * 4u + (c->latch & 3u));
        set_prg16(c, 1, c->outer * 4u + 3);
        set_chr(c, 0, 8, 0);
        break;
    case 75: vrc1_update(c); break;
    case 32: g101_update(c); break;
    case 33: case 48: case 80: case 207: taito_update(c); break;
    case 24: case 26: vrc6_update(c); break;
    case 85: vrc7_update(c); break;
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
    case 19: case 210: n163_update(c); break;
    case 9: case 10: mmc2_update(c); break;
    case 18: case 69: banks8_update(c); break;
    case 16: case 153: case 157: case 159: fcg_update(c); break;
    }
}

int cart_load(Cart *c, const uint8_t *d, size_t size, char *err, size_t errlen) {
    memset(c, 0, sizeof *c);
    if (cart_is_fds(d, size)) return fds_load(c, d, size, err, errlen);
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
    /* NES 2.0 byte 12: 0 NTSC, 1 PAL, 2 multi-region (run as NTSC), 3 Dendy */
    c->region = nes2 ? ((d[12] & 3) == 2 ? REGION_NTSC : d[12] & 3) : (!junk && (d[9] & 1)) ? REGION_PAL : REGION_NTSC;
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
    static const int SUPPORTED[] = {0, 1, 2, 3, 4, 5, 7, 9, 10, 11, 13, 16, 18, 19, 21, 22, 23, 24, 25, 26, 32,
                                    33, 34, 37, 41, 47, 48, 64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 75, 76, 77,
                                    78, 79, 80, 82, 85, 86, 87, 88, 89, 92, 93, 94, 95, 97, 113, 118, 119, 140,
                                    152, 153, 154, 155, 157, 158, 159, 180, 184, 185, 188, 206, 207, 210, 228, 232};
    int ok = 0;
    for (size_t i = 0; i < sizeof SUPPORTED / sizeof *SUPPORTED; i++) ok |= c->mapper == SUPPORTED[i];
    if (!ok) {
        snprintf(err, errlen, "mapper %d not supported yet", c->mapper);
        return -1;
    }
    /* CHR RAM after the CHR ROM: 8 KB without CHR ROM, CPROM's 16 KB, and the
     * 8 KB that boards 77 and 119 have besides their ROM */
    c->chr_rom_size = c->chr_size;
    uint32_t chr_ram = c->mapper == 13 ? 0x4000 : (c->mapper == 77 || c->mapper == 119 || !c->chr_size) ? 0x2000 : 0;
    c->chr_ram = chr_ram != 0;
    c->chr_size += chr_ram;
    c->prg = malloc(c->prg_size);
    c->chr = malloc(c->chr_size + 0x400);  /* + a page of $FF: CHR disabled (mapper 185) */
    memset(c->chr, 0, c->chr_size);
    memset(c->chr + c->chr_size, 0xFF, 0x400);
    memcpy(c->prg, d + off, c->prg_size);
    memcpy(c->chr, d + off + c->prg_size, c->chr_rom_size);
    if (c->mapper == 1 || c->mapper == 155) c->reg[0] = 0x0C;  /* power-on: last bank fixed at $C000 */
    if (cart_is_mmc3(c) || c->mapper == 206 || c->mapper == 76 || c->mapper == 88 || c->mapper == 95 ||
        c->mapper == 154) { c->reg[6] = 0; c->reg[7] = 1; c->reg[15] = 0xFE; }
    if (c->mapper == 34 && c->submapper != 1 && c->submapper != 2) c->submapper = c->chr_rom_size > 0x2000 ? 1 : 2;
    if (c->mapper == 65) { c->prg_bank[1] = 1; c->prg_bank[2] = 0xFE; }
    if (c->mapper == 188) c->latch = 0x10;  /* the first bank at $8000 */
    c->chr_latch[0] = c->chr_latch[1] = 1;  /* MMC2/MMC4: $FE (power-on state unknown) */
    if (c->mapper == 32 && c->submapper == 1 && !c->four_screen) c->mirroring = MIRROR_SINGLE_HIGH;
    c->vrc2 = c->mapper == 22 || ((c->mapper == 23 || c->mapper == 25) && c->submapper == 3);
    c->prg_ram_size = 0x2000;
    /* power-on RAM is not zero on hardware; Famicom Jump II (153) jams on
     * all-zero RAM with no save (nesdev), $FF boots it */
    if (c->mapper == 153) memset(c->prg_ram, 0xFF, 0x2000);
    c->n163_sound = c->mapper == 19;
    if (is_x1005(c)) c->prg_ram_size = 128;  /* the chip's own RAM (battery-backed on some boards) */
    if (c->mapper == 82) c->prg_ram_size = 0x1400;  /* X1-017: $6000-$73FF */
    if (c->mapper == 5) {
        /* NES 2.0 byte 10: PRG RAM and NVRAM sizes (64 << shift); else the most a board has */
        uint32_t ram = (d[10] & 15) ? 64u << (d[10] & 15) : 0, nv = (d[10] >> 4) ? 64u << (d[10] >> 4) : 0;
        uint32_t total = nes2 ? ram + nv : 0;
        c->prg_ram_size = total >= 0x2000 && total <= 0x10000 ? total : 0x10000;
        c->mmc5_prg_mode = 3;
        c->mmc5_prg[4] = 0xFF;  /* power-on: the last bank at $E000 (reset vector) */
    }
    /* Bandai EEPROMs, kept in prg_ram (saved like battery RAM): 159 an X24C01;
     * 157 the Datach's 24C02, plus a cartridge X24C01 if the NES 2.0 header
     * lists NVRAM (Battle Rush); 16 a 24C02 on an LZ93D50 with NVRAM in the
     * header, or, without a submapper, a battery. */
    int nvram = nes2 && (d[10] >> 4);
    int c02 = c->mapper == 157 || (c->mapper == 16 && (c->submapper == 5 ? nvram : c->submapper == 0 && c->battery));
    int c01 = c->mapper == 159 || (c->mapper == 157 && nvram);
    if (c02 || c01) {
        uint16_t base = 0;
        if (c02) { c->eeprom[c->eeprom_count++] = (Eeprom){.size = 256, .base = base, .scl = 1, .sda = 1, .out = 1}; base = 256; }
        if (c01) c->eeprom[c->eeprom_count++] = (Eeprom){.size = 128, .base = base, .c01 = 1, .scl = 1, .sda = 1, .out = 1};
        c->prg_ram_size = base + (c01 ? 128u : 0u);
        c->battery = 1;
    }
    update_banks(c);
    return 0;
}

void cart_free(Cart *c) {
    free(c->prg);
    free(c->chr);
    free(c->disk);
    c->prg = c->chr = c->disk = NULL;
}

uint8_t cart_cpu_read(Cart *c, uint16_t a, uint8_t open_bus) {
    if (a >= 0x8000) {
        int w = (a >> 13) & 3;
        return ((c->prg_ram_window >> w & 1) ? c->prg_ram : c->prg)[c->prg_off[w] + (a & 0x1FFF)];
    }
    if (a >= 0x6000 && c->eeprom_count) {  /* bit 4: the EEPROMs' SDA (open drain: ANDed) */
        int sda = c->eeprom[0].out & (c->eeprom_count > 1 ? c->eeprom[1].out : 1);
        return (uint8_t)((open_bus & (c->mapper == 157 ? 0xE7 : 0xEF)) | sda << 4);
    }
    if (a >= 0x6000 && is_bandai(c) && c->mapper != 153) return open_bus;
    if (a >= 0x6000 && is_x1005(c)) return a >= 0x7F00 && c->latch == 0xA3 ? c->prg_ram[a & 0x7F] : open_bus;
    if (a >= 0x6000 && c->mapper == 82) return x1017_ram_on(c, a) ? c->prg_ram[a - 0x6000] : open_bus;
    if (a >= 0x6000 && c->mapper == 188) return (uint8_t)((open_bus & 0xF8) | 3);  /* microphone: buttons up */
    if (a >= 0x6000 && cart_6000_is_io(c) && c->mapper != 34 && c->mapper != 87) return open_bus;  /* registers only */
    if (a >= 0x6000) return (c->rom_at_6000 ? c->prg : c->prg_ram)[c->ram_off + (a - 0x6000)];
    if (c->mapper == 5 && a >= 0x5000) return mmc5_read(c, a, open_bus);
    if (c->mapper == 19) return n163_read(c, a, open_bus);
    if (c->mapper == 20) return fds_read(c, a, open_bus);
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
        else c->reg[c->mmc3_select & (is_rambo(c) ? 15 : 7)] = v;
        mmc3_update(c);
        break;
    case 1:
        if (!odd && !c->four_screen && c->mapper != 118 && c->mapper != 158)
            c->mirroring = (v & 1) ? MIRROR_HORIZONTAL : MIRROR_VERTICAL;
        if (odd) c->ram_ctrl = v;  /* PRG RAM enable/protect: only the multicarts' $6000 register heeds it */
        break;
    case 2:
        if (!odd) c->irq_latch = v;
        else {
            c->irq_counter = 0;
            c->irq_reload = 1;
            if (is_rambo(c)) c->irq_ctrl = v & 1, c->irq_prescale = 0;  /* bit 0: count CPU cycles / 4 */
        }
        break;
    case 3:
        if (!odd) { c->irq_enabled = 0; c->irq_line = 0; c->irq_due_dot = 0; }
        else c->irq_enabled = 1;
        break;
    }
}

int cart_cpu_write(Cart *c, uint16_t a, uint8_t v, uint64_t cycle) {
    if (c->mapper == 20 && a < 0x6000) {  /* FDS registers: timer and drive run to now first */
        cart_run(c, cycle);
        fds_write(c, a, v);
        return 0;
    }
    if (a < 0x8000 && cart_is_reg(c, a)) {  /* 79: $4100 (A8 set), 87: $6000-$7FFF, 5, 19: $5xxx */
        if (c->mapper == 5) return mmc5_write(c, a, v);
        if (c->mapper == 19) { n163_write(c, a, v); return 0; }
        if (c->mapper == 16) { fcg_write(c, a, v); return 1; }
        if (is_x1005(c)) { x1005_write(c, a, v); return 1; }
        if (c->mapper == 82) { x1017_write(c, a, v); update_banks(c); return 1; }
        if (c->mapper == 34) { c->prg_ram[a - 0x6000] = v; c->reg[a - 0x7FFD] = v; update_banks(c); return 1; }
        if (c->mapper == 37 || c->mapper == 47) {  /* while $A001 enables PRG RAM writes */
            if ((c->ram_ctrl & 0xC0) == 0x80) { c->outer = v; mmc3_update(c); }
            return 1;
        }
        if (c->mapper == 41) {  /* the address is the value */
            c->outer = a & 0x3F;
            if (!c->four_screen) c->mirroring = (a & 0x20) ? MIRROR_HORIZONTAL : MIRROR_VERTICAL;
            update_banks(c);
            return 1;
        }
        c->latch = v;
        if (c->mapper == 113) latch_mirroring(c, v);
        update_banks(c);
        return c->mapper != 87 && c->mapper != 184;  /* 87, 184 switch CHR only */
    }
    if (a < 0x6000) return 0;
    if (a < 0x8000) {
        if (c->mapper == 34 || (c->mapper == 82 && x1017_ram_on(c, a))) c->prg_ram[a - 0x6000] = v;
        else if (!c->rom_at_6000 && !cart_6000_is_io(c)) c->prg_ram[c->ram_off + (a - 0x6000)] = v;
        else if (is_x1005(c) && a >= 0x7F00 && c->latch == 0xA3) c->prg_ram[a & 0x7F] = v;
        return 0;
    }
    if (c->prg_ram_window >> ((a >> 13) & 3) & 1) {  /* MMC5: PRG RAM in a ROM window */
        c->prg_ram[c->prg_off[(a >> 13) & 3] + (a & 0x1FFF)] = v;
        return 0;
    }
    switch (c->mapper) {
    case 0: case 5: case 20: case 79: case 87: case 82: case 86: case 113: case 140: case 184: return 0;
    case 19: n163_write(c, a, v); return 1;
    case 1: case 155: mmc1_write(c, a, v, cycle); return 1;
    case 4: case 118: case 37: case 47: case 64: case 119: case 158: mmc3_write(c, a, v); return 1;
    case 65: h3001_write(c, a, v); return 1;
    case 67: sunsoft3_write(c, a, v); update_banks(c); return 1;
    case 68: sunsoft4_write(c, a, v); update_banks(c); return 1;
    case 73: vrc3_write(c, a, v); update_banks(c); return 1;
    case 34: if (c->submapper == 1) return 0; break;  /* NINA-001: registers at $7FFD-$7FFF */
    case 41: if (!(c->outer & 4)) return 0; break;   /* inner CHR: only in the upper 4 games */
    case 72: case 92:
        if (v & 0x80) c->prg_bank[0] = v & 0x0F;
        if (v & 0x40) c->chr_bank[0] = v & 0x0F;
        update_banks(c);
        return 1;
    case 97: if (a >= 0xC000) return 0; break;
    case 228: c->addr_latch = a; if (!c->four_screen) c->mirroring = (a & 0x2000) ? MIRROR_HORIZONTAL : MIRROR_VERTICAL; break;
    case 232:
        if (a < 0xC000) {
            c->outer = c->submapper == 1 ? ((v >> 4) & 1) | ((v >> 2) & 2) : (v >> 3) & 3;
            update_banks(c);
            return 1;
        }
        break;
    case 21: case 22: case 23: case 25: vrc_write(c, a, v); return 1;
    case 9: case 10: mmc2_write(c, a, v); return 1;
    case 18: ss88006_write(c, a, v); return 1;
    case 75: vrc1_write(c, a, v); return 1;
    case 32: g101_write(c, a, v); return 1;
    case 33: case 48: taito_write(c, a, v); return 1;
    case 24: case 26: vrc6_write(c, a, v); return 1;
    case 85: vrc7_write(c, a, v); return 1;
    case 80: case 207: return 0;  /* registers at $7EFx */
    case 69: fme7_write(c, a, v); return 1;  /* command 8 can map ROM or RAM at $6000 */
    case 210: n175_write(c, a, v); return 1;
    case 16: if (c->submapper == 4) return 0; /* FCG-1/2: $6000-$7FFF only */ /* fall through */
    case 153: case 157: case 159: fcg_write(c, a, v); return 1;
    case 206: case 76: case 88: case 95: case 154:  /* bank select and data only; 3 select bits */
        if (a < 0xA000) mmc3_write(c, a, (a & 1) ? v : v & 7);
        if (c->mapper == 154) latch_mirroring(c, v);
        return 1;
    case 71:  /* $C000: PRG; $9000: single-screen select (BF9097 boards) */
        if (a >= 0xC000) { c->latch = v; update_banks(c); }
        else if (a >= 0x9000 && a < 0xA000) c->mirroring = (v & 0x10) ? MIRROR_SINGLE_HIGH : MIRROR_SINGLE_LOW;
        return 1;
    }
    c->latch = v;  /* simple latch mappers (bus conflicts ignored) */
    latch_mirroring(c, v);
    update_banks(c);
    return 1;
}

void cart_chr_write(Cart *c, uint16_t a, uint8_t v) {
    uint32_t o = c->chr_off[(a >> 10) & 7] + (a & 0x3FF);
    if (c->chr_ram && o >= c->chr_rom_size && o < c->chr_size) c->chr[o] = v;
}

void cart_ppu_address(Cart *c, uint16_t a, uint64_t dot) {
    if (!cart_is_mmc3(c)) return;
    if (c->irq_due_dot && dot >= c->irq_due_dot) { c->irq_due_dot = 0; c->irq_line = 1; }  /* TC0690 */
    uint8_t a12 = (a >> 12) & 1;
    if (a12 && !c->a12) {
        if (dot - c->a12_low_since >= A12_FILTER_DOTS && !(is_rambo(c) && (c->irq_ctrl & 1))) mmc3_clock(c, dot);
    } else if (!a12 && c->a12) {
        c->a12_low_since = dot;
    }
    c->a12 = a12;
}

int cart_irq_armed(const Cart *c) {
    return cart_is_mmc3(c) && c->irq_enabled && (c->irq_counter <= 1 || c->irq_reload) &&
           !(is_rambo(c) && (c->irq_ctrl & 1));
}
