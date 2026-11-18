/* Famicom Disk System, between cart.c and fds.c (the rest of the program
 * uses cart.h: cart_is_fds, cart_fds_bios, cart_fds_insert). */
#ifndef NES_FDS_H
#define NES_FDS_H

#include "cart.h"

int fds_load(Cart *c, const uint8_t *d, size_t size, char *err, size_t errlen);
void fds_run(Cart *c, int64_t cycles);  /* the timer and the drive, `cycles` CPU cycles on */
int64_t fds_cycles_to_irq(const Cart *c);
uint8_t fds_read(Cart *c, uint16_t a, uint8_t open_bus);    /* $4020-$5FFF */
void fds_write(Cart *c, uint16_t a, uint8_t v);              /* $4020-$5FFF */

#endif
