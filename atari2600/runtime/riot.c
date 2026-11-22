/* RIOT register and timer behavior informed by Stella's M6532.cxx.
 * Stella, Copyright (c) 1995-2026 Bradford W. Mott, Stephen Anthony and
 * the Stella Team, GPL-2.0-or-later. See docs/sources.md. */
#include "riot.h"

#include <string.h>

void a26_riot_init(A26Riot *riot) {
    memset(riot, 0, sizeof *riot);
    riot->input_a = riot->input_b = 0xFF;
    riot->divider = 1024;
    riot->pa7_sync = riot->pa7_previous = 1;
}

void a26_riot_tick(A26Riot *riot) {
    riot->wrapped = 0;
    riot->phase = (riot->phase + 1) % riot->divider;
    if ((riot->flags & 0x80) || riot->phase == 0) {
        if (riot->timer-- == 0) {
            riot->flags |= 0x80;
            riot->wrapped = 1;
        }
    }
    uint8_t pin = (riot->ddr_a & 0x80) ? (riot->out_a >> 7) : (riot->input_a >> 7);
    uint8_t stable = riot->pa7_sync;
    riot->pa7_sync = pin & 1;
    if (stable != riot->pa7_previous && stable == riot->edge_positive)
        riot->flags |= 0x40;
    riot->pa7_previous = stable;
}

uint8_t a26_riot_read(A26Riot *riot, uint16_t address) {
    if (!(address & 0x200)) return riot->ram[address & 0x7F];
    switch (address & 7) {
    case 0: return (uint8_t)((riot->out_a | ~riot->ddr_a) & riot->input_a);
    case 1: return riot->ddr_a;
    case 2: return (uint8_t)((riot->out_b | ~riot->ddr_b) & (riot->input_b | riot->ddr_b));
    case 3: return riot->ddr_b;
    case 4: case 6:
        if (!riot->wrapped) riot->flags &= (uint8_t)~0x80;
        return riot->timer;
    default: {
        uint8_t result = riot->flags;
        riot->flags &= (uint8_t)~0x40;
        return result;
    }
    }
}

void a26_riot_write(A26Riot *riot, uint16_t address, uint8_t value) {
    if (!(address & 0x200)) {
        riot->ram[address & 0x7F] = value;
    } else if (!(address & 4)) {
        switch (address & 3) {
        case 0: riot->out_a = value; break;
        case 1: riot->ddr_a = value; break;
        case 2: riot->out_b = value; break;
        case 3: riot->ddr_b = value; break;
        }
    } else if (address & 0x10) {
        static const unsigned dividers[] = {1, 8, 64, 1024};
        riot->divider = dividers[address & 3];
        riot->phase = riot->divider - 1;
        riot->timer = value;
        riot->flags &= (uint8_t)~0x80;
    } else {
        riot->edge_positive = address & 1;
    }
}
