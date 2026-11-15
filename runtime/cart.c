/* iNES parsing follows recomp/ines.py (nesdev "INES"). */
#include "cart.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int cart_load(Cart *c, const uint8_t *d, size_t size, char *err, size_t errlen) {
    memset(c, 0, sizeof *c);
    if (size < 16 || memcmp(d, "NES\x1a", 4) != 0) {
        snprintf(err, errlen, "not an iNES file");
        return -1;
    }
    c->mapper = (d[6] >> 4) | (d[7] & 0xF0);
    c->mirroring = (d[6] & 0x08) ? MIRROR_FOUR : (d[6] & 0x01) ? MIRROR_VERTICAL : MIRROR_HORIZONTAL;
    c->prg_size = d[4] * 0x4000u;
    c->chr_size = d[5] * 0x2000u;
    size_t off = 16 + ((d[6] & 0x04) ? 512 : 0);
    if (c->prg_size == 0 || size < off + c->prg_size + c->chr_size) {
        snprintf(err, errlen, "file shorter than its header says");
        return -1;
    }
    if (c->mapper != 0) {
        snprintf(err, errlen, "mapper %d not supported yet", c->mapper);
        return -1;
    }
    c->chr_ram = c->chr_size == 0;
    if (c->chr_ram) c->chr_size = 0x2000;
    c->prg = malloc(c->prg_size);
    c->chr = calloc(1, c->chr_size);
    memcpy(c->prg, d + off, c->prg_size);
    if (!c->chr_ram) memcpy(c->chr, d + off + c->prg_size, c->chr_size);
    return 0;
}

void cart_free(Cart *c) {
    free(c->prg);
    free(c->chr);
    c->prg = c->chr = NULL;
}

uint8_t cart_cpu_read(Cart *c, uint16_t a, uint8_t open_bus) {
    if (a >= 0x8000) return c->prg[(a - 0x8000) % c->prg_size];  /* 16 KB mirrors */
    if (a >= 0x6000) return c->prg_ram[a - 0x6000];
    return open_bus;
}

void cart_cpu_write(Cart *c, uint16_t a, uint8_t v) {
    if (a >= 0x6000 && a < 0x8000) c->prg_ram[a - 0x6000] = v;
}

uint8_t cart_chr_read(Cart *c, uint16_t a) { return c->chr[a & 0x1FFF]; }

void cart_chr_write(Cart *c, uint16_t a, uint8_t v) {
    if (c->chr_ram) c->chr[a & 0x1FFF] = v;
}

uint16_t cart_nt_index(Cart *c, uint16_t a) {
    a &= 0x0FFF;
    switch (c->mirroring) {
    case MIRROR_VERTICAL: return a & 0x07FF;                         /* $2000=$2800 */
    case MIRROR_HORIZONTAL: return (a & 0x03FF) | ((a & 0x0800) >> 1); /* $2000=$2400 */
    default: return a;                                                /* four-screen */
    }
}
