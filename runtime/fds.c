/* Famicom Disk System: the RAM adapter's registers, IRQ timer and the disk
 * drive (nesdev "Family Computer Disk System", "FDS disk format"). The drive
 * model follows Mesen2 (Core/NES/Mappers/FDS, GPL-3 like this program):
 * the image is turned into the bytes on the disk surface (a 28300-bit lead-in
 * gap, then each block after a $80 start mark, with 2 CRC bytes and a 976-bit
 * gap after it), the motor needs 50000 CPU cycles to reach the first byte,
 * then a byte passes the head every 149 cycles. Reads report no CRC errors.
 * shortcut: the sound channel ($4040-$4097) is not emulated yet. */
#include "fds.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { LEAD_IN = 28300 / 8, BLOCK_GAP = 976 / 8, SPIN_UP = 50000, BYTE_CYCLES = 149 };

static const uint8_t *bios;

void cart_fds_bios(const uint8_t *b) { bios = b; }

int cart_is_fds(const uint8_t *d, size_t size) {
    if (size >= 16 && !memcmp(d, "FDS\x1a", 4)) return 1;
    return size >= FDS_SIDE && size % FDS_SIDE == 0 && d[0] == 1 && !memcmp(d + 1, "*NINTENDO-HVC*", 14);
}

/* One side of the image (FDS_SIDE bytes: blocks back to back) as on the disk.
 * Returns its length. Unknown data: the rest of the side as one block. */
static uint32_t build_side(uint8_t *out, const uint8_t *src) {
    uint32_t pos = LEAD_IN, j = 0, file_size = 0;
    while (j < FDS_SIDE) {
        uint32_t len;
        switch (src[j]) {
        case 1: len = 56; break;   /* disk info */
        case 2: len = 2; break;    /* file count */
        case 3: len = 16; file_size = src[j + 13] | src[j + 14] << 8; break;  /* file header */
        case 4: len = 1 + file_size; break;  /* file data */
        default:
            out[pos++] = 0x80;
            memcpy(out + pos, src + j, FDS_SIDE - j);
            return pos + FDS_SIDE - j;
        }
        if (j + len >= FDS_SIDE || pos + 3 + len + BLOCK_GAP > FDS_RAW_SIDE) break;
        out[pos++] = 0x80;
        memcpy(out + pos, src + j, len);
        pos += len;
        out[pos++] = 0x4D, out[pos++] = 0x62;  /* CRC: not checked on reads */
        pos += BLOCK_GAP;
        j += len;
    }
    return pos > FDS_SIDE ? pos : FDS_SIDE;
}

int fds_load(Cart *c, const uint8_t *d, size_t size, char *err, size_t errlen) {
    int header = !memcmp(d, "FDS\x1a", 4);
    size_t sides = header ? d[4] : size / FDS_SIDE;
    if (header) d += 16, size -= 16;
    if (sides == 0 || sides > FDS_MAX_SIDES || size < sides * FDS_SIDE) {
        snprintf(err, errlen, "not a Famicom Disk System image");
        return -1;
    }
    if (!bios) {
        snprintf(err, errlen, "Famicom Disk System BIOS needed (disksys.rom)");
        return -1;
    }
    c->mapper = 20;
    c->region = REGION_NTSC;
    c->mirroring = MIRROR_VERTICAL;
    c->prg_size = 0x2000;
    c->prg = malloc(c->prg_size);
    memcpy(c->prg, bios, c->prg_size);
    c->chr_ram = 1;
    c->chr_size = 0x2000;
    c->chr = malloc(c->chr_size + 0x400);
    memset(c->chr, 0, c->chr_size);
    memset(c->chr + c->chr_size, 0xFF, 0x400);
    for (int i = 0; i < 8; i++) c->chr_off[i] = i * 0x400u;
    c->prg_ram_size = 0x8000;  /* $6000-$DFFF */
    c->prg_ram_window = 7;     /* $8000, $A000, $C000: RAM; $E000: the BIOS */
    for (int w = 0; w < 3; w++) c->prg_off[w] = (w + 1) * 0x2000u;
    c->disk = calloc(sides, FDS_RAW_SIDE);
    c->disk_sides = (uint8_t)sides;
    uint32_t h = 2166136261u;  /* FNV-1a of the image */
    for (size_t i = 0; i < sides * FDS_SIDE; i++) h = (h ^ d[i]) * 16777619u;
    c->disk_hash = h;
    for (size_t s = 0; s < sides; s++) c->disk_len[s] = build_side(c->disk + s * FDS_RAW_SIDE, d + s * FDS_SIDE);
    c->fds_io = 3;
    c->fds_gap_ended = 1;
    c->disk_side = 0;  /* side A in the drive at power-on */
    c->disk_next = -1;
    return 0;
}

void cart_fds_insert(Cart *c, int side, uint64_t at) {
    c->disk_side = -1;
    c->disk_next = (int8_t)(side >= 0 && side < c->disk_sides ? side : -1);
    c->disk_insert_at = at;
}

static void update_irq(Cart *c) { c->irq_line = c->fds_timer_irq | c->fds_disk_irq; }

static void crc_add(Cart *c, uint8_t v) {
    c->fds_crc ^= v;
    for (int i = 0; i < 8; i++) c->fds_crc = (uint16_t)((c->fds_crc >> 1) ^ ((c->fds_crc & 1) ? 0x8408 : 0));
}

/* The drive when a byte passes the head (Mesen2 Fds::ProcessCpuClock). */
static void disk_byte(Cart *c) {
    uint8_t *side = c->disk + (size_t)c->disk_side * FDS_RAW_SIDE;
    int irq = c->fds_ctrl & 0x80, crc_ctrl = c->fds_ctrl & 0x10, ready = c->fds_ctrl & 0x40;
    c->fds_scanning = 1;
    if (c->fds_ctrl & 0x04) {  /* read */
        uint8_t v = side[c->disk_pos];
        if (!c->fds_prev_crc) crc_add(c, v);
        if (!ready) {
            c->fds_gap_ended = 0;
            c->fds_crc = 0;
            c->fds_bad_crc = 0;
        } else if (v && !c->fds_gap_ended) {  /* the $80 start mark ends the gap */
            c->fds_gap_ended = 1;
            irq = 0;
        }
        if (c->fds_gap_ended) {
            c->fds_transfer = 1;
            c->fds_rdata = v;
            if (irq) c->fds_disk_irq = 1;
        }
        if (!c->fds_prev_crc && crc_ctrl) c->fds_bad_crc = c->fds_crc != 0;
    } else {  /* write */
        uint8_t v = 0;
        if (!crc_ctrl) {
            c->fds_transfer = 1;
            v = c->fds_wdata;
            if (irq) c->fds_disk_irq = 1;
        }
        if (!ready) v = 0, c->fds_crc = 0;
        if (!crc_ctrl) crc_add(c, v);
        else v = (uint8_t)c->fds_crc, c->fds_crc >>= 8;
        if (side[c->disk_pos] != v) side[c->disk_pos] = v, c->disk_dirty = 1;
        c->fds_gap_ended = 0;
        c->fds_bad_crc = 0;
    }
    c->fds_prev_crc = (uint8_t)crc_ctrl;
    if (++c->disk_pos >= c->disk_len[c->disk_side]) {
        c->fds_motor = 0;  /* end of the disk */
        if (c->fds_ctrl & 0x80) c->fds_disk_irq = 1;
    } else {
        c->disk_delay = BYTE_CYCLES;
    }
}

/* Cycles from now in which neither the timer nor the drive does anything,
 * for the bulk skip in fds_run (0: something happens this cycle). */
static int64_t quiet_cycles(const Cart *c, int64_t limit) {
    int64_t k = limit;
    if (c->fds_timer_on && c->fds_counter < k) k = c->fds_counter;
    int spinning = c->disk_side >= 0 && c->fds_motor;
    if (spinning && !((c->fds_ctrl & 0x02) && !c->fds_scanning)) {  /* not held in transfer reset */
        if (c->fds_end_of_head) k = 0;
        else if (c->disk_delay < k) k = c->disk_delay;
    }
    return k;
}

void fds_run(Cart *c, int64_t n) {
    if (c->disk_side < 0 && c->disk_next >= 0 && c->irq_cycle >= c->disk_insert_at) {
        c->disk_side = c->disk_next;
        c->disk_next = -1;
    }
    while (n > 0) {
        if (c->disk_side < 0 || !c->fds_motor) c->fds_end_of_head = 1, c->fds_scanning = 0;
        int64_t k = quiet_cycles(c, n);
        if (k > 0) {
            if (c->fds_timer_on) c->fds_counter = (uint16_t)(c->fds_counter - k);
            if (c->disk_side >= 0 && c->fds_motor && !((c->fds_ctrl & 0x02) && !c->fds_scanning) && !c->fds_end_of_head)
                c->disk_delay -= (uint32_t)k;
            n -= k;
            continue;
        }
        /* one cycle with an event: the timer, then the drive (Mesen2's order) */
        if (c->fds_timer_on) {
            if (c->fds_counter == 0) {
                c->fds_timer_irq = 1;
                c->fds_counter = c->fds_reload;
                if (!c->fds_repeat) c->fds_timer_on = 0;
            } else {
                c->fds_counter--;
            }
        }
        if (c->disk_side >= 0 && c->fds_motor && !((c->fds_ctrl & 0x02) && !c->fds_scanning)) {
            if (c->fds_end_of_head) {
                c->disk_delay = SPIN_UP;
                c->fds_end_of_head = 0;
                c->disk_pos = 0;
                c->fds_gap_ended = 0;
            } else if (c->disk_delay > 0) {
                c->disk_delay--;
            } else {
                disk_byte(c);
            }
        }
        n--;
    }
    update_irq(c);
}

int64_t fds_cycles_to_irq(const Cart *c) {
    int64_t best = -1;
    if (c->fds_timer_on) best = c->fds_counter + 1;
    int spinning = c->disk_side >= 0 && c->fds_motor && !((c->fds_ctrl & 0x02) && !c->fds_scanning);
    if (spinning && (c->fds_ctrl & 0x80)) {  /* the next byte may raise the disk IRQ */
        int64_t d = c->fds_end_of_head ? 1 + SPIN_UP + 1 : (int64_t)c->disk_delay + 1;
        if (best < 0 || d < best) best = d;
    }
    return best;
}

uint8_t fds_read(Cart *c, uint16_t a, uint8_t open_bus) {
    if (!(c->fds_io & 1) || a < 0x4030 || a > 0x4033) return open_bus;
    uint8_t v;
    switch (a) {
    case 0x4030:  /* bits 2 and 5 are open bus */
        v = (uint8_t)((open_bus & 0x24) | c->fds_timer_irq | c->fds_transfer << 1 |
                      (c->mirroring == MIRROR_HORIZONTAL) << 3);
        c->fds_transfer = c->fds_timer_irq = c->fds_disk_irq = 0;
        break;
    case 0x4031:
        v = c->fds_rdata;
        c->fds_transfer = c->fds_disk_irq = 0;
        break;
    case 0x4032: {  /* bit 0: no disk, 1: not ready, 2: write protected (or no disk) */
        int none = c->disk_side < 0;
        v = (uint8_t)((open_bus & 0xF8) | none | (none || !c->fds_scanning) << 1 | none << 2);
        break;
    }
    default: v = c->fds_ext; break;  /* $4033: bit 7 = battery good, as written to $4026 */
    }
    update_irq(c);
    return v;
}

void fds_write(Cart *c, uint16_t a, uint8_t v) {
    if (!(c->fds_io & 1) && a >= 0x4024 && a <= 0x4026) return;
    switch (a) {
    case 0x4020: c->fds_reload = (uint16_t)((c->fds_reload & 0xFF00) | v); break;
    case 0x4021: c->fds_reload = (uint16_t)((c->fds_reload & 0x00FF) | v << 8); break;
    case 0x4022:
        c->fds_repeat = v & 1;
        c->fds_timer_on = (v & 2) && (c->fds_io & 1);
        if (c->fds_timer_on) c->fds_counter = c->fds_reload;
        else c->fds_timer_irq = 0;
        break;
    case 0x4023:
        c->fds_io = v;
        if (!(v & 1)) c->fds_timer_on = c->fds_timer_irq = c->fds_disk_irq = 0;
        break;
    case 0x4024:
        c->fds_wdata = v;
        c->fds_transfer = c->fds_disk_irq = 0;
        break;
    case 0x4025:
        c->fds_ctrl = v;
        c->fds_motor = v & 1;
        c->mirroring = (v & 8) ? MIRROR_HORIZONTAL : MIRROR_VERTICAL;
        c->fds_disk_irq = 0;
        break;
    case 0x4026: c->fds_ext = v; break;
    default: break;
    }
    update_irq(c);
}
