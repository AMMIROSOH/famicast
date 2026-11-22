#ifndef ATARI2600_BUS_H
#define ATARI2600_BUS_H

#include "cart.h"
#include "cpu6502.h"
#include "riot.h"

/* TIA callbacks are invoked once per access and once per color clock. */
typedef struct {
    Cpu cpu;
    A26Cart cart;
    A26Riot riot;
    void *tia;
    uint8_t (*tia_read)(void *tia, uint16_t address, uint8_t open_bus);
    void (*tia_write)(void *tia, uint16_t address, uint8_t value);
    void (*tia_tick)(void *tia);
    int (*tia_ready)(void *tia);
} A26Bus;

void a26_bus_init(A26Bus *bus);

#endif
