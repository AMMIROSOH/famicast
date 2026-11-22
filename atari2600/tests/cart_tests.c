#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "cart.h"

int main(void) {
    uint8_t rom[32768];
    A26Cart cart;
    for (unsigned bank = 0; bank < 8; bank++)
        memset(rom + bank * 4096, (int)bank, 4096);

    assert(a26_cart_init(&cart, rom, 2048, A26_CART_AUTO) == 0);
    rom[0x123] = 0xA5;
    assert(a26_cart_read(&cart, 0x1123) == 0xA5);
    assert(a26_cart_read(&cart, 0x1923) == 0xA5);
    assert(a26_cart_init(&cart, rom, 4096, A26_CART_AUTO) == 0);
    assert(a26_cart_read(&cart, 0x1123) == 0xA5);

    const size_t sizes[] = {8192, 16384, 32768};
    const unsigned counts[] = {2, 4, 8};
    const unsigned first[] = {0xFF8, 0xFF6, 0xFF4};
    for (unsigned type = 0; type < 3; type++) {
        assert(a26_cart_init(&cart, rom, sizes[type], A26_CART_AUTO) == 0);
        assert(cart.bank == counts[type] - 1);
        for (unsigned bank = 0; bank < counts[type]; bank++) {
            assert(a26_cart_read(&cart, (uint16_t)(0x1000 + first[type] + bank)) == bank);
            a26_cart_write(&cart, (uint16_t)(0xF000 + first[type] + bank), 0);
            assert(cart.bank == bank);
        }
        a26_cart_reset(&cart);
        assert(cart.bank == counts[type] - 1);
    }
    assert(a26_cart_init(&cart, rom, 4096, A26_CART_F8) == -1);
    assert(a26_cart_init(&cart, rom, 1234, A26_CART_AUTO) == -1);
    puts("Atari 2600 cartridge tests passed");
    return 0;
}
