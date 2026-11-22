#include "cart.h"

int a26_cart_init(A26Cart *cart, const uint8_t *rom, size_t size, A26CartType type) {
    if (!cart || !rom) return -1;
    if (type == A26_CART_AUTO) {
        switch (size) {
        case 2048: type = A26_CART_2K; break;
        case 4096: type = A26_CART_4K; break;
        case 8192: type = A26_CART_F8; break;
        case 16384: type = A26_CART_F6; break;
        case 32768: type = A26_CART_F4; break;
        default: return -1;
        }
    }
    unsigned banks;
    size_t expected;
    switch (type) {
    case A26_CART_2K: banks = 1; expected = 2048; break;
    case A26_CART_4K: banks = 1; expected = 4096; break;
    case A26_CART_F8: banks = 2; expected = 8192; break;
    case A26_CART_F6: banks = 4; expected = 16384; break;
    case A26_CART_F4: banks = 8; expected = 32768; break;
    default: return -1;
    }
    if (size != expected) return -1;
    *cart = (A26Cart){rom, size, type, banks - 1, banks};
    return 0;
}

void a26_cart_reset(A26Cart *cart) {
    cart->bank = cart->banks - 1;
}

/* Hotspot selection occurs on both reads and writes, including dummy cycles. */
static void select_bank(A26Cart *cart, uint16_t address) {
    unsigned offset = address & 0x0FFF;
    unsigned first = 0x0FF8;
    if (cart->type == A26_CART_F6) first = 0x0FF6;
    if (cart->type == A26_CART_F4) first = 0x0FF4;
    if (cart->banks > 1 && offset >= first && offset < first + cart->banks)
        cart->bank = offset - first;
}

uint8_t a26_cart_read(A26Cart *cart, uint16_t address) {
    select_bank(cart, address);
    unsigned offset = address & (cart->type == A26_CART_2K ? 0x07FF : 0x0FFF);
    return cart->rom[cart->bank * 4096 + offset];
}

void a26_cart_write(A26Cart *cart, uint16_t address, uint8_t value) {
    (void)value;
    select_bank(cart, address);
}
