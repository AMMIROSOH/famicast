/* Cartridge: iNES loading and mapper. M3: NROM (mapper 0) only; the
 * function set below is what other mappers will implement in M7. */
#ifndef NES_CART_H
#define NES_CART_H

#include <stddef.h>
#include <stdint.h>

enum { MIRROR_HORIZONTAL, MIRROR_VERTICAL, MIRROR_FOUR };

typedef struct Cart {
    uint8_t *prg, *chr;
    uint32_t prg_size, chr_size;
    int chr_ram, mapper, mirroring;
    uint8_t prg_ram[0x2000];  /* $6000-$7FFF (blargg tests report here) */
} Cart;

/* Returns 0 on success, else writes a message to err. Copies the data. */
int cart_load(Cart *c, const uint8_t *data, size_t size, char *err, size_t errlen);
void cart_free(Cart *c);

/* CPU $4020-$FFFF. open_bus is returned for unmapped addresses. */
uint8_t cart_cpu_read(Cart *c, uint16_t a, uint8_t open_bus);
void cart_cpu_write(Cart *c, uint16_t a, uint8_t v);
/* PPU $0000-$1FFF */
uint8_t cart_chr_read(Cart *c, uint16_t a);
void cart_chr_write(Cart *c, uint16_t a, uint8_t v);
/* PPU $2000-$3EFF -> index into the PPU's 4 KB nametable RAM */
uint16_t cart_nt_index(Cart *c, uint16_t a);

#endif
