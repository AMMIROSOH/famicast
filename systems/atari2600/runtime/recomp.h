#ifndef ATARI2600_RECOMP_H
#define ATARI2600_RECOMP_H

#include <stddef.h>
#include <stdint.h>

#include "cpu6502.h"

typedef void (*A26RecompFn)(Cpu *cpu);
typedef struct {
    uint32_t rom_hash;
    const uint8_t *rom;
    size_t rom_size;
    const char *rom_name;
    int mapper, pal;
    A26RecompFn (*lookup)(unsigned bank, unsigned offset);
} A26RecompProgram;

extern const A26RecompProgram *const a26_recomp_program;

#endif
