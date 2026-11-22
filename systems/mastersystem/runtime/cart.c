/* The cartridge: the "TMR SEGA" header and the mappers (SMS Power,
 * Development: Mappers). Three 16 KB slots at $0000, $4000 and $8000.
 * Sega's: banked by writes to $FFFD-$FFFF; the first 1 KB stays on bank 0
 * so the vectors survive banking; $FFFC bit 3 maps cartridge RAM over slot
 * 2, bit 2 picks which of its two 16 KB banks (sms.c). Codemasters': a
 * write to $0000, $4000 or $8000 picks that slot's bank, no fixed 1 KB. */
#include <string.h>

#include "sms.h"

long sms_header(const uint8_t *data, size_t size) {
    static const long at[] = {0x1FF0, 0x3FF0, 0x7FF0};
    for (unsigned i = 0; i < sizeof at / sizeof at[0]; i++)
        if ((size_t)at[i] + 16 <= size && !memcmp(data + at[i], "TMR SEGA", 8))
            return at[i];
    return -1;
}

/* Codemasters games carry their own header: a checksum at $7FE6 and its
 * complement to $10000 at $7FE8 (SMS Power, ROM header). */
int sms_is_codemasters(const uint8_t *d, size_t size) {
    if (size < 0x8000) return 0;
    unsigned sum = d[0x7FE6] | d[0x7FE7] << 8, inv = d[0x7FE8] | d[0x7FE9] << 8;
    return sum && ((sum + inv) & 0xFFFF) == 0;
}

uint32_t sms_cart_addr(const SmsCart *c, uint16_t a) {
    if (a < 0x0400 && !c->codemasters) return a;
    unsigned banks = (unsigned)((c->size + SMS_ROM_BANK - 1) / SMS_ROM_BANK);
    unsigned bank = banks ? c->page[1 + (a >> 14)] % banks : 0;
    return (uint32_t)bank * SMS_ROM_BANK + (a & (SMS_ROM_BANK - 1));
}
