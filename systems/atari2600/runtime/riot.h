#ifndef ATARI2600_RIOT_H
#define ATARI2600_RIOT_H

#include <stdint.h>

typedef struct {
    uint8_t ram[128];
    uint8_t out_a, out_b, ddr_a, ddr_b;
    uint8_t input_a, input_b;
    uint8_t timer, flags, wrapped;
    uint8_t edge_positive, pa7_sync, pa7_previous;
    unsigned divider, phase;
} A26Riot;

void a26_riot_init(A26Riot *riot);
void a26_riot_tick(A26Riot *riot);
uint8_t a26_riot_read(A26Riot *riot, uint16_t address);
void a26_riot_write(A26Riot *riot, uint16_t address, uint8_t value);

#endif
