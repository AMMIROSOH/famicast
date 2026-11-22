#ifndef ATARI2600_CART_H
#define ATARI2600_CART_H

#include <stddef.h>
#include <stdint.h>

typedef enum {
    A26_CART_AUTO, A26_CART_2K, A26_CART_4K,
    A26_CART_F8, A26_CART_F6, A26_CART_F4
} A26CartType;

/* The caller owns the ROM bytes for the lifetime of the cartridge. */
typedef struct {
    const uint8_t *rom;
    size_t size;
    A26CartType type;
    unsigned bank;
    unsigned banks;
} A26Cart;

/* Returns -1 for an unsupported size, type, or size/type combination. */
int a26_cart_init(A26Cart *cart, const uint8_t *rom, size_t size, A26CartType type);
void a26_cart_reset(A26Cart *cart);
uint8_t a26_cart_read(A26Cart *cart, uint16_t address);
void a26_cart_write(A26Cart *cart, uint16_t address, uint8_t value);

#endif
